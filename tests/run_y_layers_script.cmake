execute_process(COMMAND "${HEADLESS}" --script
    INPUT_FILE "${SCENARIO}"
    OUTPUT_VARIABLE output ERROR_VARIABLE error RESULT_VARIABLE result)
if(NOT result EQUAL 0
    OR NOT output MATCHES "tick=400 position=-3,0,-1"
    OR NOT output MATCHES "tick=600 position=-3,0,1"
    OR NOT output MATCHES "tick=600 advanced=0 The way is blocked"
    OR NOT output MATCHES "tick=800 position=-4,1,-1"
    OR NOT output MATCHES "tick=900 advanced=1 You pick up the item"
    OR NOT output MATCHES "inspect tick=900 .*invariants=ok"
    OR NOT output MATCHES "actor id=1 position=-4,1,-1 turns=9 inventory=1")
    message(FATAL_ERROR "Y-layer script failed (${result}): ${output}${error}")
endif()
