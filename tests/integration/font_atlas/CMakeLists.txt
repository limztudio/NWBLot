nwb_declare_gtest_executable(nwb_font_atlas_bake_tests)
target_sources(nwb_font_atlas_bake_tests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/packing_tests.cpp")
target_link_libraries(nwb_font_atlas_bake_tests PRIVATE nwb_font_atlas_bake)

add_test(NAME nwb_font_atlas_cli_integration
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/font_atlas_cli_tests.py"
        --executable "$<TARGET_FILE:nwb_font_atlas>"
        --fonts "${PROJECT_SOURCE_DIR}/impl/assets/ui/fonts/default"
)
set_tests_properties(nwb_font_atlas_cli_integration PROPERTIES TIMEOUT 240)
