nwb_declare_executable(nwb_assets_graphics_tests)
target_sources(nwb_assets_graphics_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/acceptance_tests.inl"
    "${CMAKE_CURRENT_LIST_DIR}/codec_tests.inl"
    "${CMAKE_CURRENT_LIST_DIR}/material_cook_tests.inl"
    "${CMAKE_CURRENT_LIST_DIR}/material_tests.inl"
    "${CMAKE_CURRENT_LIST_DIR}/mesh_cooker_tests.inl"
    "${CMAKE_CURRENT_LIST_DIR}/shader_tests.inl"
)
target_compile_definitions(nwb_assets_graphics_tests PRIVATE NWB_COOK=1)
target_link_libraries(nwb_assets_graphics_tests PRIVATE
    nwb_assets_graphics_cook
    nwb_common
    nwb_alloc
)
add_test(NAME nwb_assets_graphics_tests COMMAND nwb_assets_graphics_tests)
set_tests_properties(nwb_assets_graphics_tests PROPERTIES RUN_SERIAL TRUE)
