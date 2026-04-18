nwb_declare_executable(nwb_assets_graphics_tests)
target_sources(nwb_assets_graphics_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/assets_graphics_tests.cpp"
)
target_compile_definitions(nwb_assets_graphics_tests PRIVATE NWB_COOK=1)
target_link_libraries(nwb_assets_graphics_tests PRIVATE
    nwb_assets_graphics_cook
    nwb_common
)
add_test(NAME nwb_assets_graphics_tests COMMAND nwb_assets_graphics_tests)
set_tests_properties(nwb_assets_graphics_tests PROPERTIES RUN_SERIAL TRUE)
