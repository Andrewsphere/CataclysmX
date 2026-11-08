# Exercise the actual command parser, process restart, autosave, and rollback to
# the last checkpoint. Relative save names keep script paths whitespace-free.
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef suffix)
set(directory "${CMAKE_CURRENT_BINARY_DIR}/save-script-${suffix}")
file(MAKE_DIRECTORY "${directory}")
file(WRITE "${directory}/create.txt"
    "autosave 2\nsave world.sqlite   # trailing whitespace is not part of the path\npickup\nwait\nwait\ninspect\n")
execute_process(COMMAND "${HEADLESS}" --script
    WORKING_DIRECTORY "${directory}" INPUT_FILE "${directory}/create.txt"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT result EQUAL 0 OR NOT output MATCHES "inspect tick=300.*invariants=ok")
    message(FATAL_ERROR "Save script failed (${result}):\n${output}\n${errors}")
endif()
file(WRITE "${directory}/load.txt"
    "load world.sqlite   # load the autosave at turn two\ninspect\nwait\nsave\nload world.sqlite\ninspect\n")
execute_process(COMMAND "${HEADLESS}" --script
    WORKING_DIRECTORY "${directory}" INPUT_FILE "${directory}/load.txt"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT result EQUAL 0 OR NOT output MATCHES "inspect tick=200.*invariants=ok"
    OR NOT output MATCHES "inspect tick=300.*invariants=ok"
    OR NOT output MATCHES "inventory=1")
    message(FATAL_ERROR "Load script failed (${result}):\n${output}\n${errors}")
endif()
if(DEFINED TERMINAL)
    execute_process(COMMAND "${TERMINAL}" --user-dir "${directory}/user" --save "${directory}/terminal world.sqlite" --render
        RESULT_VARIABLE result OUTPUT_VARIABLE before ERROR_VARIABLE errors)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Terminal save failed: ${errors}")
    endif()
    execute_process(COMMAND "${TERMINAL}" --user-dir "${directory}/user" --load "${directory}/terminal world.sqlite" --render
        RESULT_VARIABLE result OUTPUT_VARIABLE after ERROR_VARIABLE errors)
    if(NOT result EQUAL 0 OR NOT before STREQUAL after)
        message(FATAL_ERROR "Terminal load did not restore the same view: ${errors}")
    endif()
endif()
file(REMOVE_RECURSE "${directory}")
