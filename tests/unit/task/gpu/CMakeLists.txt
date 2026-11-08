nwb_declare_gtest_executable(nwb_gpu_task_tests)
nwb_target_adhd_sources(nwb_gpu_task_tests NAMESPACE "NWB::Tests::TaskGraphTestUtils" SOURCES "${CMAKE_CURRENT_LIST_DIR}/case_types.adhd")
target_sources(nwb_gpu_task_tests PRIVATE
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

target_link_libraries(nwb_gpu_task_tests PRIVATE nwb_gpu_task nwb_common nwb_alloc)
