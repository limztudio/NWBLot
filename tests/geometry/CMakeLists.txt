nwb_declare_executable(nwb_geometry_tests)
target_sources(nwb_geometry_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/geometry_tests.cpp"
)
target_link_libraries(nwb_geometry_tests PRIVATE
    nwb_geometry
    nwb_common
    nwb_alloc
)
add_test(NAME nwb_geometry_tests COMMAND nwb_geometry_tests)
