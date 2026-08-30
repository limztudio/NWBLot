if(TARGET nwb_assets_volume_cook)
    add_subdirectory(assets_graphics)
endif()

if(TARGET nwb_crash)
    add_subdirectory(crash)
endif()

if(TARGET nwb_fbx_to_nwb)
    add_subdirectory(fbx_to_nwb)
endif()

add_subdirectory(logger_server)

if((TARGET testbed OR (TARGET nwb_loader AND TARGET nwb_resource_cooker)) AND Python3_Interpreter_FOUND)
    # Keep the existing CTest name stable while its process-and-file capture coverage lives with integration tests.
    add_test(
        NAME nwb_testbed_window_capture_unit
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_LIST_DIR}/window_capture_smoke_tests.py"
    )
endif()

if(TARGET nwb_tex_conv)
    add_subdirectory(tex_conv)
endif()

if(TARGET nwb_resource_cooker AND Python3_Interpreter_FOUND)
    add_test(
        NAME nwb_resource_cooker_unsupported_asset_type_integration
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_LIST_DIR}/resource_cooker_cli_tests.py"
            --executable "$<TARGET_FILE:nwb_resource_cooker>"
            --repo-root "${PROJECT_SOURCE_DIR}"
    )
    set_tests_properties(nwb_resource_cooker_unsupported_asset_type_integration PROPERTIES TIMEOUT 20)
endif()
