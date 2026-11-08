cmake_minimum_required(VERSION 3.25)
project(CataclysmX VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(CATACLYSMX_BUILD_TESTS "Build the Catch2 integration harness" ON)
option(CATACLYSMX_BUILD_TERMINAL "Build the FTXUI adapter and manual probe" ON)
option(CATACLYSMX_ENABLE_ASAN "Enable MSVC AddressSanitizer" OFF)
option(CATACLYSMX_ENABLE_ASAN_UBSAN "Enable Clang/GCC ASan and UBSan" OFF)

find_package(EASTL CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(yyjson CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)

function(cataclysmx_target target)
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
    src/core/integration.cpp src/core/world.cpp src/core/simulation.cpp src/core/server.cpp
    src/core/save_database.cpp src/core/save_database.h src/core/save_result.h src/core/world_archive.h
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

add_library(cataclysmx_client STATIC src/client/text.cpp src/client/text.h)
target_link_libraries(cataclysmx_client PUBLIC cataclysmx_engine)
cataclysmx_no_exceptions(cataclysmx_client)

add_executable(cataclysmx_headless src/headless/main.cpp)
target_link_libraries(cataclysmx_headless PRIVATE cataclysmx_client)
cataclysmx_no_exceptions(cataclysmx_headless)

if(CATACLYSMX_BUILD_TERMINAL)
    find_package(ftxui CONFIG REQUIRED)
    add_library(cataclysmx_terminal STATIC src/terminal/render.cpp src/terminal/render.h)
    target_include_directories(cataclysmx_terminal PUBLIC src)
    target_link_libraries(cataclysmx_terminal PRIVATE ftxui::screen)
    cataclysmx_no_exceptions(cataclysmx_terminal)
    add_executable(cataclysmx_terminal_smoke tools/terminal_smoke/main.cpp)
    target_link_libraries(cataclysmx_terminal_smoke PRIVATE cataclysmx_terminal ftxui::component)
    cataclysmx_no_exceptions(cataclysmx_terminal_smoke)
    add_executable(cataclysmx_game src/terminal/main.cpp)
    target_link_libraries(cataclysmx_game PRIVATE cataclysmx_client ftxui::component)
    cataclysmx_no_exceptions(cataclysmx_game)
endif()

if(CATACLYSMX_BUILD_TESTS)
    enable_testing()
    find_package(Catch2 3 CONFIG REQUIRED)
    add_executable(cataclysmx_tests tests/integration_tests.cpp tests/world_tests.cpp tests/server_tests.cpp
        tests/save_database_tests.cpp tests/world_save_tests.cpp tests/server_save_tests.cpp)
    target_link_libraries(cataclysmx_tests PRIVATE cataclysmx_engine Catch2::Catch2WithMain unofficial::sqlite3::sqlite3)
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
    set(save_client_terminal_arg)
    if(CATACLYSMX_BUILD_TERMINAL)
        set(save_client_terminal_arg "-DTERMINAL=$<TARGET_FILE:cataclysmx_game>")
    endif()
    add_test(NAME save_client_roundtrip COMMAND ${CMAKE_COMMAND}
        "-DHEADLESS=$<TARGET_FILE:cataclysmx_headless>"
        ${save_client_terminal_arg}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_save_script.cmake")
    if(CATACLYSMX_BUILD_TERMINAL)
        add_test(NAME terminal_game_render COMMAND cataclysmx_game --render)
        set_tests_properties(terminal_game_render PROPERTIES PASS_REGULAR_EXPRESSION "tick=0 position=0,0")
    endif()
    # Confirm a failed REQUIRE actually returns a failure, without making CTest red.
    add_test(NAME catch2_expected_failure COMMAND cataclysmx_tests "[.intentional-failure]")
    set_tests_properties(catch2_expected_failure PROPERTIES WILL_FAIL TRUE)
    add_test(NAME logging_fail_fast COMMAND cataclysmx_headless --fail-log)
    set_tests_properties(logging_fail_fast PROPERTIES
        PASS_REGULAR_EXPRESSION "SPDLOG_NO_EXCEPTIONS verified")
endif()
