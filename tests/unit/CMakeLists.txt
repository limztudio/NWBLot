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
    # Keep the natural hybrid-versus-opaque report schema, route diagnostics, and shared-binary launcher contract
    # under ordinary CTest. The window/timestamp capture remains an opt-in target-hardware workflow.
    add_test(
        NAME nwb_hybrid_shadow_boundary_harness_unit
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tests/ab/hybrid_shadow_boundary/run.py"
            "--self-test"
    )
    add_test(
        NAME nwb_hybrid_shadow_boundary_launcher_unit
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tests/ab/hybrid_shadow_boundary/launch.py"
            "--self-test"
    )
    # Parser/state-machine coverage for the opt-in target-hardware lagged-lighting smoke. The actual run remains
    # manual because it requires a dedicated Vulkan compute family plus an interactive native window.
    add_test(
        NAME nwb_frame_lagged_async_lighting_harness_unit
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tests/ab/frame_lagged_async_lighting/run.py"
            "--self-test"
    )
    add_test(
        NAME nwb_frame_lagged_async_lighting_launcher_unit
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tests/ab/frame_lagged_async_lighting/launch.py"
            "--self-test"
    )
    # The transfer-upload A/B stays opt-in because a meaningful run needs a distinct Transfer-only family and an
    # external bandwidth capture. Keep its parser/report contracts in regular unit coverage.
    add_test(
        NAME nwb_transfer_queue_harness_unit
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tests/ab/transfer_queue/run.py"
            "--self-test"
    )
    add_test(
        NAME nwb_transfer_queue_launcher_unit
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tests/ab/transfer_queue/launch.py"
            "--self-test"
    )
endif()
