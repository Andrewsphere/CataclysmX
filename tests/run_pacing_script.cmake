execute_process(COMMAND "${HEADLESS}" --script
    INPUT_FILE "${SCENARIO}"
    OUTPUT_VARIABLE output ERROR_VARIABLE error RESULT_VARIABLE result)
string(REGEX MATCHALL "tick=200 skipped=0 needs-input=1" idleUpdates "${output}")
list(LENGTH idleUpdates idleUpdateCount)
if(NOT result EQUAL 0
    OR NOT output MATCHES "tick=50 skipped=0 needs-input=0"
    OR NOT output MATCHES "pacing rate=100 limit=on idle-pause=on speed=100 delay=150"
    OR NOT output MATCHES "pacing rate=200 limit=on idle-pause=on speed=100 delay=100"
    OR idleUpdateCount LESS 2
    OR NOT output MATCHES "tick=[0-9]+ skipped=[1-9][0-9]* needs-input=[01]"
    OR NOT output MATCHES "delay=100 queued=yes"
    OR NOT output MATCHES "resolved tick=500 advanced=1 You move."
    OR NOT output MATCHES "tick=500 position=0,1"
    OR NOT output MATCHES "delay=100 queued=no"
    OR NOT output MATCHES "tick=600 skipped=0 needs-input=1")
    message(FATAL_ERROR "Pacing script failed (${result}):\n${output}\n${error}")
endif()
