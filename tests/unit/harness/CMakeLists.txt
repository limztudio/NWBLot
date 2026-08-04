if((TARGET testbed OR (TARGET nwb_loader AND TARGET nwb_resource_cooker)) AND Python3_Interpreter_FOUND)
    add_test(
        NAME nwb_launcher_unit
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_LIST_DIR}/launcher_tests.py"
    )
    add_test(
        NAME nwb_testbed_window_capture_unit
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_LIST_DIR}/testbed_window_capture_tests.py"
    )
    # Pure-Python parser/verdict coverage for the manual M4 runner. The actual benchmark deliberately remains an
    # opt-in target-hardware workflow because it requires a distinct Vulkan compute family and a visible window.
    add_test(
        NAME nwb_async_shadow_m4_harness_unit
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tests/ab/async_shadow_m4/run.py"
            "--self-test"
    )
    add_test(
        NAME nwb_async_shadow_m4_launcher_unit
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tests/ab/async_shadow_m4/launch.py"
            "--self-test"
    )
    # Parser/state-machine coverage for the opt-in target-hardware lagged-lighting smoke. The actual run remains
    # manual because it requires a dedicated Vulkan compute family plus an interactive native window.
    add_test(
        NAME nwb_frame_lagged_async_lighting_harness_unit
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tests/ab/frame_lagged_async_lighting/launch.py"
            "--self-test"
    )
endif()
