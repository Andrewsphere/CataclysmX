execute_process(COMMAND "${HEADLESS}" --script
    INPUT_FILE "${SCENARIO}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Item scenario failed (${result}):\n${output}\n${errors}")
endif()
if(NOT output MATCHES "energy=3/1000"
    OR NOT output MATCHES "ground item=8.*parent=7 installation=1"
    OR NOT output MATCHES "active=no remaining=0"
    OR NOT output MATCHES "position=3,0"
    OR NOT output MATCHES "invariants=ok")
    message(FATAL_ERROR "Item scenario missed expected charging, installation or door effects:\n${output}")
endif()
