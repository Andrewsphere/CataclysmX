cmake_minimum_required(VERSION 3.25)
project(CataclysmX VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(CATACLYSMX_BUILD_TESTS "Build the Catch2 integration harness" ON)
option(CATACLYSMX_BUILD_TERMINAL "Build the FTXUI adapter and manual probe" ON)
option(CATACLYSMX_BUILD_SDL "Build the SDL3 prototype frontend" OFF)
option(CATACLYSMX_ENABLE_ASAN "Enable MSVC AddressSanitizer" OFF)
option(CATACLYSMX_ENABLE_ASAN_UBSAN "Enable Clang/GCC ASan and UBSan" OFF)
option(CATACLYSMX_ENABLE_TSAN "Enable Linux ThreadSanitizer (separate from ASan)" OFF)

if(CATACLYSMX_ENABLE_TSAN AND (NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR
    CATACLYSMX_ENABLE_ASAN OR CATACLYSMX_ENABLE_ASAN_UBSAN))
    message(FATAL_ERROR "TSan requires Linux and cannot combine with ASan/UBSan")
endif()

find_package(EASTL CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(yyjson CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(unofficial-enet CONFIG REQUIRED)
find_package(Threads REQUIRED)

function(cataclysmx_target target)
    if(CATACLYSMX_ENABLE_TSAN)
        target_compile_options(${target} PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=thread)
    endif()
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(CATACLYSMX_ENABLE_ASAN)
        if(NOT MSVC OR CATACLYSMX_ENABLE_ASAN_UBSAN)
            message(FATAL_ERROR "CATACLYSMX_ENABLE_ASAN requires MSVC and cannot combine with ASan/UBSan")
        endif()
        # Debug's /RTC1 and incremental linking are incompatible with MSVC ASan.
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            message(FATAL_ERROR "Use RelWithDebInfo for MSVC ASan (see win-msvc-asan preset)")
        endif()
        target_compile_options(${target} PRIVATE /fsanitize=address)
        # vcpkg dependencies are not ASan-instrumented. MSVC requires matching STL
        # annotation settings across linked objects (including Catch2 and FTXUI).
        # Keep ASan memory instrumentation, but omit STL container annotations.
        target_compile_definitions(${target} PRIVATE _DISABLE_STL_ANNOTATION)
        target_link_options(${target} PRIVATE /INCREMENTAL:NO)
    endif()
    if(CATACLYSMX_ENABLE_ASAN_UBSAN)
        if(MSVC)
            message(FATAL_ERROR "ASan/UBSan requires Clang or GCC")
        endif()
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
endfunction()

function(cataclysmx_no_exceptions target)
    cataclysmx_target(${target})
    if(MSVC)
        target_compile_options(${target} PRIVATE /EHs-c-)
        target_compile_definitions(${target} PRIVATE _HAS_EXCEPTIONS=0)
    else()
        target_compile_options(${target} PRIVATE -fno-exceptions)
    endif()
endfunction()

add_library(cataclysmx_engine STATIC
    src/content/content.cpp src/content/content.h
    src/content/item_definition.cpp src/content/item_definition.h
    src/core/data_object.cpp src/core/data_object.h src/core/item_state.cpp src/core/item_state.h
    src/core/world_types.h
    src/core/world_result.h src/content/content_result.h
    src/items/item_types.h src/items/item_types.cpp
    src/items/item_behavior.h src/items/item_behavior.cpp
    src/items/item_registry.h src/items/item_registry.cpp
    src/items/item_system.h src/items/item_system.cpp
    src/items/item_snapshot.h src/items/item_snapshot.cpp
    src/items/charger_behavior.cpp src/items/door_controller_behavior.cpp
    src/core/diagnostics.cpp src/core/diagnostics.h
    src/core/integration.cpp src/core/world.cpp src/core/simulation.cpp src/core/server.cpp
    src/core/pacing.cpp src/core/pacing.h src/core/timing.h
    src/core/save_database.cpp src/core/save_database.h src/core/save_result.h src/core/world_archive.h
    src/worldgen/worldgen.cpp src/worldgen/worldgen.h
    src/core/containers.h src/core/integration.h src/core/server.h
    src/core/simulation.h src/core/slot_map.h src/core/world.h)
target_include_directories(cataclysmx_engine PUBLIC src)
target_include_directories(cataclysmx_engine SYSTEM PRIVATE
    third_party/unordered_dense/include third_party/slotmap)
target_link_libraries(cataclysmx_engine PUBLIC EASTL PRIVATE spdlog::spdlog yyjson::yyjson unofficial::sqlite3::sqlite3)
# Public EASTL containers must have the same inline definitions in engine and
# exception-enabled Catch2 translation units. Catch2 itself keeps exceptions.
target_compile_definitions(cataclysmx_engine PUBLIC EASTL_EXCEPTIONS_ENABLED=0)
cataclysmx_no_exceptions(cataclysmx_engine)

add_library(cataclysmx_network STATIC src/network/network.cpp src/network/network.h
    src/network/wire.cpp src/network/wire.h)
target_link_libraries(cataclysmx_network PUBLIC cataclysmx_engine PRIVATE unofficial::enet::enet)
if(WIN32)
    # ENet's static vcpkg target does not propagate its WinSock/timer imports.
    target_link_libraries(cataclysmx_network PRIVATE ws2_32 winmm)
endif()
cataclysmx_no_exceptions(cataclysmx_network)

add_library(cataclysmx_client STATIC src/client/text.cpp src/client/text.h
    src/client/worldgen_debug.cpp src/client/worldgen_debug.h
    src/client/lan_profile.cpp src/client/lan_profile.h
    src/client/input.cpp src/client/input.h src/client/settings.cpp src/client/settings.h
    src/client/localization.cpp src/client/localization.h src/content/preferences.cpp)
target_link_libraries(cataclysmx_client PUBLIC cataclysmx_engine PRIVATE yyjson::yyjson)
cataclysmx_no_exceptions(cataclysmx_client)

add_executable(cataclysmx_headless src/headless/main.cpp src/headless/lan.cpp src/headless/lan.h)
target_compile_definitions(cataclysmx_headless PRIVATE CATACLYSMX_TERRAIN_FILE="${CMAKE_CURRENT_SOURCE_DIR}/data/terrain.json5")
target_link_libraries(cataclysmx_headless PRIVATE cataclysmx_client cataclysmx_network Threads::Threads)
cataclysmx_no_exceptions(cataclysmx_headless)

if(CATACLYSMX_BUILD_TERMINAL)
    find_package(ftxui CONFIG REQUIRED)
    add_library(cataclysmx_terminal STATIC src/terminal/render.cpp src/terminal/render.h
        src/terminal/gameplay_screen.cpp src/terminal/gameplay_screen.h
        src/terminal/debug_menu.cpp src/terminal/debug_menu.h
        src/terminal/keyboard.cpp src/terminal/keyboard.h
        src/terminal/settings_menu.cpp src/terminal/settings_menu.h)
    target_include_directories(cataclysmx_terminal PUBLIC src)
    target_link_libraries(cataclysmx_terminal PUBLIC cataclysmx_client ftxui::dom ftxui::component PRIVATE ftxui::screen)
    cataclysmx_no_exceptions(cataclysmx_terminal)
    add_executable(cataclysmx_terminal_smoke tools/terminal_smoke/main.cpp)
    target_link_libraries(cataclysmx_terminal_smoke PRIVATE cataclysmx_terminal ftxui::component)
    cataclysmx_no_exceptions(cataclysmx_terminal_smoke)
    add_executable(cataclysmx_game src/terminal/main.cpp)
    target_compile_definitions(cataclysmx_game PRIVATE CATACLYSMX_TERRAIN_FILE="${CMAKE_CURRENT_SOURCE_DIR}/data/terrain.json5")
    target_compile_definitions(cataclysmx_game PRIVATE CATACLYSMX_LOC_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/data/loc")
    target_link_libraries(cataclysmx_game PRIVATE cataclysmx_client cataclysmx_network cataclysmx_terminal ftxui::component Threads::Threads)
    cataclysmx_no_exceptions(cataclysmx_game)
endif()

if(CATACLYSMX_BUILD_SDL)
    find_package(SDL3 CONFIG REQUIRED)
    find_package(SDL3_image CONFIG REQUIRED)
    find_package(SDL3_ttf CONFIG REQUIRED)
    add_library(cataclysmx_sdl STATIC
        src/content/tileset.cpp src/content/tileset.h
        src/content/tileset_source.cpp src/content/tileset_source.h
        src/sdl/atlas_builder.cpp src/sdl/atlas_builder.h
        src/sdl/input.cpp src/sdl/input.h
        src/sdl/channels.cpp src/sdl/channels.h
        src/sdl/session.cpp src/sdl/session.h
        src/sdl/assets.cpp src/sdl/assets.h
        src/sdl/text.cpp src/sdl/text.h
        src/sdl/map_packet.cpp src/sdl/map_packet.h
        src/sdl/item_visual.cpp src/sdl/item_visual.h
        src/sdl/animation.cpp src/sdl/animation.h
        src/sdl/renderer.cpp src/sdl/renderer.h
        src/sdl/app.cpp src/sdl/app.h)
    target_link_libraries(cataclysmx_sdl PUBLIC cataclysmx_client SDL3::SDL3 SDL3_ttf::SDL3_ttf Threads::Threads
        PRIVATE SDL3_image::SDL3_image yyjson::yyjson)
    target_compile_definitions(cataclysmx_sdl PRIVATE
        CATACLYSMX_DEFAULT_TILESET="${CMAKE_CURRENT_SOURCE_DIR}/assets/tiles/gen1_32x32"
        CATACLYSMX_DEFAULT_FONT="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/DejaVuSansMono.ttf")
    cataclysmx_no_exceptions(cataclysmx_sdl)
    add_executable(cataclysmx_sdl_game src/sdl/main.cpp)
    target_compile_definitions(cataclysmx_sdl_game PRIVATE
        CATACLYSMX_TERRAIN_FILE="${CMAKE_CURRENT_SOURCE_DIR}/data/terrain.json5"
        CATACLYSMX_LOC_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/data/loc")
    target_link_libraries(cataclysmx_sdl_game PRIVATE cataclysmx_sdl)
    cataclysmx_no_exceptions(cataclysmx_sdl_game)
endif()

if(CATACLYSMX_BUILD_TESTS)
    enable_testing()
    find_package(Catch2 3 CONFIG REQUIRED)
    add_executable(cataclysmx_tests tests/integration_tests.cpp tests/world_tests.cpp tests/server_tests.cpp
        tests/save_database_tests.cpp tests/world_save_tests.cpp tests/server_save_tests.cpp
        tests/content_tests.cpp tests/diagnostics_tests.cpp tests/client_tests.cpp
        tests/terrain_integration_tests.cpp tests/building_pattern_tests.cpp tests/worldgen_tests.cpp
        tests/worldgen_debug_tests.cpp tests/pacing_tests.cpp tests/scheduling_tests.cpp
        tests/multiplayer_session_tests.cpp tests/player_parking_tests.cpp tests/network_tests.cpp
        tests/lan_profile_tests.cpp
        tests/item_content_tests.cpp tests/item_state_tests.cpp tests/item_world_tests.cpp
        tests/item_save_tests.cpp tests/item_network_tests.cpp tests/item_server_tests.cpp
        tests/item_behavior_tests.cpp
        tests/settings_tests.cpp tests/localization_tests.cpp)
    target_compile_definitions(cataclysmx_tests PRIVATE CATACLYSMX_TERRAIN_FILE="${CMAKE_CURRENT_SOURCE_DIR}/data/terrain.json5")
    target_compile_definitions(cataclysmx_tests PRIVATE CATACLYSMX_LOC_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/data/loc")
    target_link_libraries(cataclysmx_tests PRIVATE cataclysmx_client cataclysmx_network Catch2::Catch2WithMain unofficial::sqlite3::sqlite3 Threads::Threads)
    if(CATACLYSMX_BUILD_SDL)
        target_sources(cataclysmx_tests PRIVATE tests/sdl_input_tests.cpp tests/sdl_app_tests.cpp
            tests/sdl_session_tests.cpp tests/sdl_render_tests.cpp tests/sdl_tileset_tests.cpp
            tests/sdl_asset_tests.cpp tests/sdl_text_tests.cpp tests/sdl_map_tests.cpp tests/sdl_animation_tests.cpp)
        target_sources(cataclysmx_tests PRIVATE tests/sdl_source_tileset_tests.cpp)
        target_link_libraries(cataclysmx_tests PRIVATE cataclysmx_sdl)
        add_test(NAME sdl_frontend_smoke COMMAND cataclysmx_sdl_game --smoke
            --user-dir "${CMAKE_CURRENT_BINARY_DIR}/test-user/sdl-smoke")
        set_tests_properties(sdl_frontend_smoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software"
            TIMEOUT 20 PASS_REGULAR_EXPRESSION "SDL smoke: ok")
    endif()
    if(CATACLYSMX_BUILD_TERMINAL)
        target_sources(cataclysmx_tests PRIVATE tests/terminal_render_tests.cpp tests/terminal_debug_menu_tests.cpp
            tests/terminal_input_tests.cpp tests/terminal_settings_tests.cpp tests/terminal_gameplay_tests.cpp
            tests/item_ui_tests.cpp)
        target_link_libraries(cataclysmx_tests PRIVATE cataclysmx_terminal)
    endif()
    cataclysmx_target(cataclysmx_tests)
    if(MSVC)
        target_compile_options(cataclysmx_tests PRIVATE /EHsc)
    else()
        target_compile_options(cataclysmx_tests PRIVATE -fexceptions)
    endif()
    include(Catch)
    catch_discover_tests(cataclysmx_tests TEST_SPEC "~[.intentional-failure]")
    add_test(NAME headless_smoke COMMAND cataclysmx_headless --probe)
    set_tests_properties(headless_smoke PROPERTIES PASS_REGULAR_EXPRESSION "checksum=3365722423")
    add_test(NAME headless_game COMMAND cataclysmx_headless)
    set_tests_properties(headless_game PROPERTIES PASS_REGULAR_EXPRESSION "invariants=ok")
    add_test(NAME headless_streaming_script COMMAND ${CMAKE_COMMAND}
        "-DHEADLESS=$<TARGET_FILE:cataclysmx_headless>"
        "-DSCENARIO=${CMAKE_CURRENT_SOURCE_DIR}/tests/scenarios/streaming.txt"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_headless_script.cmake")
    add_test(NAME headless_invalid_script COMMAND ${CMAKE_COMMAND}
        "-DHEADLESS=$<TARGET_FILE:cataclysmx_headless>"
        "-DSCENARIO=${CMAKE_CURRENT_SOURCE_DIR}/tests/scenarios/invalid.txt"
        -DINVALID=ON -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_headless_script.cmake")
    add_test(NAME headless_pacing_script COMMAND ${CMAKE_COMMAND}
        "-DHEADLESS=$<TARGET_FILE:cataclysmx_headless>"
        "-DSCENARIO=${CMAKE_CURRENT_SOURCE_DIR}/tests/scenarios/pacing.txt"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_pacing_script.cmake")
    add_test(NAME headless_items_script COMMAND ${CMAKE_COMMAND}
        "-DHEADLESS=$<TARGET_FILE:cataclysmx_headless>"
        "-DSCENARIO=${CMAKE_CURRENT_SOURCE_DIR}/tests/scenarios/items.txt"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_item_script.cmake")
    add_test(NAME headless_y_layers_script COMMAND ${CMAKE_COMMAND}
        "-DHEADLESS=$<TARGET_FILE:cataclysmx_headless>"
        "-DSCENARIO=${CMAKE_CURRENT_SOURCE_DIR}/tests/scenarios/y_layers.txt"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_y_layers_script.cmake")
    set(save_client_terminal_arg)
    if(CATACLYSMX_BUILD_TERMINAL)
        set(save_client_terminal_arg "-DTERMINAL=$<TARGET_FILE:cataclysmx_game>")
    endif()
    add_test(NAME save_client_roundtrip COMMAND ${CMAKE_COMMAND}
        "-DHEADLESS=$<TARGET_FILE:cataclysmx_headless>"
        ${save_client_terminal_arg}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_save_script.cmake")
    find_program(CATACLYSMX_PWSH NAMES pwsh)
    if(CATACLYSMX_PWSH)
        add_test(NAME headless_lan_roundtrip COMMAND "${CATACLYSMX_PWSH}" -NoProfile -File
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_lan_smoke.ps1"
            -Headless "$<TARGET_FILE:cataclysmx_headless>"
            -WorkDirectory "${CMAKE_CURRENT_BINARY_DIR}")
        set_tests_properties(headless_lan_roundtrip PROPERTIES TIMEOUT 45 LABELS "network;integration")
    endif()
    if(CATACLYSMX_BUILD_TERMINAL)
        add_test(NAME terminal_game_render COMMAND cataclysmx_game --user-dir "${CMAKE_CURRENT_BINARY_DIR}/test-user/terminal-render" --render)
        set_tests_properties(terminal_game_render PROPERTIES PASS_REGULAR_EXPRESSION "tick=0 position=0,0")
        add_test(NAME settings_client_roundtrip COMMAND ${CMAKE_COMMAND}
            "-DHEADLESS=$<TARGET_FILE:cataclysmx_headless>"
            "-DTERMINAL=$<TARGET_FILE:cataclysmx_game>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_settings_script.cmake")
    endif()
    # Confirm a failed REQUIRE actually returns a failure, without making CTest red.
    add_test(NAME catch2_expected_failure COMMAND cataclysmx_tests "[.intentional-failure]")
    set_tests_properties(catch2_expected_failure PROPERTIES WILL_FAIL TRUE)
    add_test(NAME logging_fail_fast COMMAND cataclysmx_headless --fail-log)
    set_tests_properties(logging_fail_fast PROPERTIES
        PASS_REGULAR_EXPRESSION "SPDLOG_NO_EXCEPTIONS verified")
endif()
