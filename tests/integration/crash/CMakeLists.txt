nwb_declare_gtest_executable(nwb_crash_tests)
target_sources(nwb_crash_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/*.cc"
    "${CMAKE_CURRENT_LIST_DIR}/*.cxx"
    "${CMAKE_CURRENT_LIST_DIR}/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.h"
    "${CMAKE_CURRENT_LIST_DIR}/*.hh"
    "${CMAKE_CURRENT_LIST_DIR}/*.hxx"
    "${CMAKE_CURRENT_LIST_DIR}/*.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.inl"
    "${CMAKE_CURRENT_LIST_DIR}/*.ipp"
    "${CMAKE_CURRENT_LIST_DIR}/*.ixx"
)
target_link_libraries(nwb_crash_tests PRIVATE nwb_crash_package nwb_common nwb_alloc)
if(TARGET nwb_crash_handler)
    add_dependencies(nwb_crash_tests nwb_crash_handler)
endif()
set_tests_properties(nwb_crash_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
