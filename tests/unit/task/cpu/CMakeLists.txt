nwb_declare_gtest_executable(nwb_cpu_task_tests)
target_sources(nwb_cpu_task_tests PRIVATE
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
target_link_libraries(nwb_cpu_task_tests PRIVATE nwb_cpu_task nwb_common)

set(_nwb_cpu_task_tests_timeout_seconds 120)
set_tests_properties(nwb_cpu_task_tests PROPERTIES TIMEOUT ${_nwb_cpu_task_tests_timeout_seconds})
unset(_nwb_cpu_task_tests_timeout_seconds)
