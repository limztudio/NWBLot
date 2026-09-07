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

if((TARGET testbed OR (TARGET nwb_loader AND TARGET nwb_asset_builder)) AND Python3_Interpreter_FOUND)
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

if(TARGET nwb_asset_builder AND Python3_Interpreter_FOUND)
    add_test(
        NAME nwb_pipeline_cli_integration
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_LIST_DIR}/pipeline_cli_tests.py"
            --dependency-computer "$<TARGET_FILE:nwb_dependeny_computer>"
            --asset-builder "$<TARGET_FILE:nwb_asset_builder>"
            --asset-gatherer "$<TARGET_FILE:nwb_asset_gatherer>"
            --pipeline-launcher "${PROJECT_SOURCE_DIR}/pipeline/launch.py"
            --repo-root "${PROJECT_SOURCE_DIR}"
    )
    set_tests_properties(nwb_pipeline_cli_integration PROPERTIES TIMEOUT 120)
endif()

if(Python3_Interpreter_FOUND)
    set(nwb_terminal_cli_arguments)
    foreach(nwb_terminal_pipeline IN ITEMS nwb_dependeny_computer nwb_asset_builder nwb_asset_gatherer)
        if(TARGET ${nwb_terminal_pipeline})
            list(APPEND nwb_terminal_cli_arguments --pipeline "$<TARGET_FILE:${nwb_terminal_pipeline}>")
        endif()
    endforeach()
    foreach(nwb_terminal_utility IN ITEMS nwb_fbx_to_nwb nwb_tex_conv)
        if(TARGET ${nwb_terminal_utility})
            list(APPEND nwb_terminal_cli_arguments --utility "$<TARGET_FILE:${nwb_terminal_utility}>")
        endif()
    endforeach()
    if(TARGET nwb_logserver)
        list(APPEND nwb_terminal_cli_arguments --application "$<TARGET_FILE:nwb_logserver>")
    endif()
    if(nwb_terminal_cli_arguments)
        add_test(NAME nwb_terminal_cli_integration
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/terminal_cli_tests.py" ${nwb_terminal_cli_arguments})
        set_tests_properties(nwb_terminal_cli_integration PROPERTIES TIMEOUT 120)
    endif()
endif()

