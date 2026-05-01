nwb_declare_executable(nwb_metascript_tests)
target_sources(nwb_metascript_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/metascript_tests.cpp"
)
target_link_libraries(nwb_metascript_tests PRIVATE
    nwb_metascript
    nwb_common
    nwb_alloc
)
add_test(NAME nwb_metascript_tests COMMAND nwb_metascript_tests)
