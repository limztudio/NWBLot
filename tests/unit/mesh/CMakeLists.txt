nwb_declare_gtest_executable(nwb_mesh_tests)
target_sources(nwb_mesh_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/mesh_tests.cpp"
)
target_link_libraries(nwb_mesh_tests PRIVATE
    nwb_mesh
    nwb_common
    nwb_alloc
)
if(NWB_BUILD_PIPELINE)
    target_sources(nwb_mesh_tests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/solid_topology_tests.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/solid_codec_tests.cpp"
    )
    target_compile_definitions(nwb_mesh_tests PRIVATE NWB_COOK=1)
    target_link_libraries(nwb_mesh_tests PRIVATE nwb_assets_mesh_cook)
endif()
