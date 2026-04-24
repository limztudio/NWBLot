find_package(Python3 COMPONENTS Interpreter)

if(Python3_Interpreter_FOUND AND TARGET testbed)
    add_test(
        NAME nwb_testbed_window_capture_smoke
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_LIST_DIR}/testbed_window_capture_smoke.py"
            "--executable" "$<TARGET_FILE:testbed>"
            "--working-directory" "${NWB_OUTPUT_ROOT}"
            "--output" "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/testbed_window_capture.bmp"
    )
    set_tests_properties(nwb_testbed_window_capture_smoke PROPERTIES
        RESOURCE_LOCK nwb_display
        SKIP_RETURN_CODE 77
        TIMEOUT 60
    )
endif()
