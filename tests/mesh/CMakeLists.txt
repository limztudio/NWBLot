nwb_declare_executable(nwb_mesh_tests)
target_sources(nwb_mesh_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/tests.cpp"
)
target_link_libraries(nwb_mesh_tests PRIVATE
    nwb_mesh
    nwb_common
    nwb_alloc
)
add_test(NAME nwb_mesh_tests COMMAND nwb_mesh_tests)
