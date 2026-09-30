add_subdirectory(csg)
add_subdirectory(ecs)
add_subdirectory(ecs_graphics)
add_subdirectory(ecs_ui)
add_subdirectory(filesystem)
add_subdirectory(global)
add_subdirectory(input)
add_subdirectory(graphics)
add_subdirectory(math)
add_subdirectory(mesh)
add_subdirectory(metascript)
add_subdirectory(model)
add_subdirectory(os)
add_subdirectory(scene)
add_subdirectory(task)
add_subdirectory(telemetry)
add_subdirectory(ui)

if((TARGET testbed OR (TARGET nwb_loader AND TARGET nwb_asset_builder)) AND Python3_Interpreter_FOUND)
    add_test(
        NAME nwb_launcher_unit
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_LIST_DIR}/repository_launcher_tests.py"
    )
    add_test(
        NAME nwb_skin_dependency_launcher_unit
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_LIST_DIR}/skin_dependency_launcher_tests.py"
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
    endforeach()
    add_test(
        NAME nwb_hybrid_shadow_boundary_launcher_unit
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tests/ab/hybrid_shadow_boundary/launch.py"
            "--self-test"
    )
endif()
