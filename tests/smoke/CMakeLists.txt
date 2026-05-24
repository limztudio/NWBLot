find_package(Python3 COMPONENTS Interpreter)

if(Python3_Interpreter_FOUND AND TARGET testbed)
    function(nwb_add_testbed_window_capture_smoke TEST_NAME OUTPUT_NAME)
        add_test(
            NAME "${TEST_NAME}"
            COMMAND
                "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_LIST_DIR}/testbed_window_capture_smoke.py"
                "--executable" "$<TARGET_FILE:testbed>"
                "--working-directory" "${NWB_OUTPUT_ROOT}"
                "--output" "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/${OUTPUT_NAME}"
                ${ARGN}
        )
        set_tests_properties("${TEST_NAME}" PROPERTIES
            RESOURCE_LOCK nwb_display
            SKIP_RETURN_CODE 77
            TIMEOUT 60
        )
    endfunction()

    nwb_add_testbed_window_capture_smoke(
        nwb_testbed_window_capture_smoke
        testbed_window_capture.bmp
    )
    nwb_add_testbed_window_capture_smoke(
        nwb_testbed_transparent_multi_capture_smoke
        testbed_transparent_multi_capture.bmp
        "--testbed-scene" "transparent_multi"
        "--expect-transparent-multi"
    )
endif()
