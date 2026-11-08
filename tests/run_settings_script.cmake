# Exercise real clients, sparse files, shared world scopes and opt-in headless
# settings. Every artifact lives in this uniquely created build subdirectory.
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef suffix)
set(directory "${CMAKE_CURRENT_BINARY_DIR}/settings-script-${suffix}")
file(MAKE_DIRECTORY "${directory}/user/config" "${directory}/world")
file(WRITE "${directory}/user/config/settings.json5"
    "{format_version:1,settings:{seed:123,view_radius:1,autosave_interval:100,ticks_per_second:123}}")
file(WRITE "${directory}/world/settings.json5"
    "{format_version:1,settings:{view_radius:2}}")
execute_process(COMMAND "${TERMINAL}" --user-dir "${directory}/user" --world-dir "${directory}/world"
    --save "${directory}/world/a.sqlite" --host --port 24680 --name SettingsHost
    --profile "${directory}/host.profile" --render
    RESULT_VARIABLE result OUTPUT_VARIABLE before ERROR_VARIABLE errors)
if(NOT result EQUAL 0 OR NOT before MATCHES "tick=0 position=0,0")
    message(FATAL_ERROR "Configured terminal create failed (${result}): ${errors}")
endif()
file(READ "${directory}/world/settings.json5" local)
if(local MATCHES "seed|autosave_interval|ticks_per_second" OR NOT local MATCHES "view_radius")
    message(FATAL_ERROR "Inherited fields were copied into world overrides: ${local}")
endif()
file(WRITE "${directory}/user/config/settings.json5"
    "{format_version:1,settings:{seed:999,view_radius:5,autosave_interval:200,ticks_per_second:321}}")
execute_process(COMMAND "${TERMINAL}" --user-dir "${directory}/user" --world-dir "${directory}/world"
    --load "${directory}/world/a.sqlite" --render
    RESULT_VARIABLE result OUTPUT_VARIABLE after ERROR_VARIABLE errors)
if(NOT result EQUAL 0 OR NOT before STREQUAL after)
    message(FATAL_ERROR "World overrides/loaded seed changed after global edit (${result}): ${errors}")
endif()
file(WRITE "${directory}/inspect.txt" "inspect\nview\n")
execute_process(COMMAND "${HEADLESS}" --user-dir "${directory}/user" --world-dir "${directory}/world"
    --load "${directory}/world/a.sqlite" --script
    INPUT_FILE "${directory}/inspect.txt" RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT result EQUAL 0 OR NOT output MATCHES "pacing rate=321" OR NOT output MATCHES "invariants=ok")
    message(FATAL_ERROR "Headless did not apply current globals (${result}): ${output}\n${errors}")
endif()
# Both option groups remain recognized, while live networking stays separate
# from deterministic scripts. This exits before opening any socket.
execute_process(COMMAND "${HEADLESS}" --user-dir "${directory}/user" --world-dir "${directory}/world"
    --script --host --run-for 1
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT result EQUAL 2 OR NOT errors MATCHES "--script and live LAN modes are separate")
    message(FATAL_ERROR "Configured headless lost its LAN/script boundary (${result}): ${errors}")
endif()
execute_process(COMMAND "${HEADLESS}" --load "${directory}/world/a.sqlite" --script
    INPUT_FILE "${directory}/inspect.txt" RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT result EQUAL 0 OR NOT output MATCHES "pacing rate=123")
    message(FATAL_ERROR "Unconfigured headless unexpectedly applied preferences (${result}): ${output}\n${errors}")
endif()
file(WRITE "${directory}/user/config/settings.json5" "{broken}")
execute_process(COMMAND "${TERMINAL}" --user-dir "${directory}/user" --render
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(result EQUAL 0 OR NOT errors MATCHES "Configuration load failed")
    message(FATAL_ERROR "Malformed settings were silently ignored: ${errors}")
endif()
file(REMOVE_RECURSE "${directory}")
