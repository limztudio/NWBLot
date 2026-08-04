nwb_declare_gtest_executable(nwb_crash_tests)
target_sources(nwb_crash_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/tests.cpp"
)
target_link_libraries(nwb_crash_tests PRIVATE
    nwb_crash_package
    nwb_common
    nwb_alloc
)
if(TARGET nwb_crash_handler)
    add_dependencies(nwb_crash_tests nwb_crash_handler)
endif()
set_tests_properties(nwb_crash_tests PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
