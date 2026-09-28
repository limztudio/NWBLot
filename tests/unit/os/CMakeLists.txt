nwb_declare_gtest_executable(nwb_os_tests)
target_sources(nwb_os_tests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/clipboard_tests.cpp")
target_link_libraries(nwb_os_tests PRIVATE nwb_os)
