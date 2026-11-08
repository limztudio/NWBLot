nwb_declare_static_library(nwb_perf)
nwb_target_adhd_sources(nwb_perf NAMESPACE "NWB::Core::Perf::CpuTaskProfileNames" SOURCES "${CMAKE_CURRENT_LIST_DIR}/cpu_task_profile_names.adhd")
nwb_declare_static_library(nwb_perf_profile_values)
nwb_target_adhd_sources(nwb_perf_profile_values NAMESPACE "NWB::Core::Perf::CpuTaskProfileValues" SOURCES "${CMAKE_CURRENT_LIST_DIR}/cpu_task_profile_values.adhd")
nwb_declare_static_library(nwb_perf_memory_source)
nwb_target_adhd_sources(nwb_perf_memory_source NAMESPACE "NWB::Core::Perf::MemorySourceTypes" SOURCES "${CMAKE_CURRENT_LIST_DIR}/memory_source_types.adhd")
target_sources(nwb_perf PRIVATE
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
target_link_libraries(nwb_perf PUBLIC nwb_alloc nwb_cpu_task)
target_link_libraries(nwb_perf PUBLIC nwb_perf_memory_source)
target_link_libraries(nwb_perf PRIVATE nwb_perf_profile_values)

