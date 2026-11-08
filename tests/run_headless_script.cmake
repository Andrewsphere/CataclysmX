execute_process(COMMAND "${HEADLESS}" --script
    INPUT_FILE "${SCENARIO}"
    OUTPUT_VARIABLE output ERROR_VARIABLE error RESULT_VARIABLE result)
if(INVALID)
    if(NOT result EQUAL 2 OR NOT error MATCHES "Invalid command/request on line 1")
        message(FATAL_ERROR "Expected syntax rejection, got ${result}: ${output}${error}")
    endif()
else()
    if(NOT result EQUAL 0
        OR NOT output MATCHES "tick=5400 position=16,2"
        OR NOT output MATCHES "Inventory: 2:backpack\\(1 inside\\)"
        OR NOT output MATCHES "inspect tick=5400 loaded=3 active=2 archived=24 invariants=ok")
        message(FATAL_ERROR "Streaming script failed (${result}): ${output}${error}")
    endif()
endif()
