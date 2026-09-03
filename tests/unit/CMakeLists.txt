add_subdirectory(csg)
add_subdirectory(ecs)
add_subdirectory(ecs_graphics)
add_subdirectory(global)
add_subdirectory(graphics)
add_subdirectory(math)
add_subdirectory(mesh)
add_subdirectory(metascript)
add_subdirectory(policy)
add_subdirectory(scene)
add_subdirectory(telemetry)

if((TARGET testbed OR (TARGET nwb_loader AND TARGET nwb_resource_cooker)) AND Python3_Interpreter_FOUND)
    add_test(
        NAME nwb_launcher_unit
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_LIST_DIR}/repository_launcher_tests.py"
    )
    # Parser/report self-tests stay in ordinary CTest. The matching GPU runs remain opt-in: M4 and lagged lighting
    # need a distinct compute family plus a window, hybrid capture is target-hardware, and transfer-queue needs a
    # Transfer-only family plus an external bandwidth capture.
    set(_nwb_ab_self_test_harnesses
        command_ir
        renderer_baseline
        async_shadow_m4
        hybrid_shadow_boundary
        frame_lagged_async_lighting
        transfer_queue
    )
    foreach(_nwb_ab_harness IN LISTS _nwb_ab_self_test_harnesses)
        add_test(
            NAME nwb_${_nwb_ab_harness}_harness_unit
            COMMAND
                "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tests/ab/${_nwb_ab_harness}/run.py"
                "--self-test"
        )
        add_test(
            NAME nwb_${_nwb_ab_harness}_launcher_unit
            COMMAND
                "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tests/ab/${_nwb_ab_harness}/launch.py"
                "--self-test"
        )
    endforeach()
endif()
