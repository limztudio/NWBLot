nwb_declare_gtest_executable(nwb_font_atlas_bake_tests)
target_sources(nwb_font_atlas_bake_tests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/packing_tests.cpp")
target_link_libraries(nwb_font_atlas_bake_tests PRIVATE nwb_font_atlas_bake)

if(TARGET nwb_assets_font_atlas_cook AND Python3_Interpreter_FOUND)
    nwb_declare_executable(nwb_font_atlas_package_probe)
    target_sources(nwb_font_atlas_package_probe PRIVATE "${CMAKE_CURRENT_LIST_DIR}/package_probe.cpp")
    target_compile_definitions(nwb_font_atlas_package_probe PRIVATE NWB_COOK=1)
    target_link_libraries(nwb_font_atlas_package_probe PRIVATE nwb_assets_font_atlas_cook nwb_common nwb_alloc nwb_logclient)

    add_test(NAME nwb_font_atlas_cli_integration
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/font_atlas_cli_tests.py"
            --executable "$<TARGET_FILE:nwb_font_atlas>"
            --package-probe "$<TARGET_FILE:nwb_font_atlas_package_probe>"
            --fonts "${PROJECT_SOURCE_DIR}/impl/assets/ui/fonts/default"
    )
    set_tests_properties(nwb_font_atlas_cli_integration PROPERTIES TIMEOUT 240)
endif()
