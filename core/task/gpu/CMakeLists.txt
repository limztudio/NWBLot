nwb_declare_static_library(nwb_gpu_task)
nwb_target_adhd_sources(nwb_gpu_task NAMESPACE "NWB::Core" SOURCES "${CMAKE_CURRENT_LIST_DIR}/queue_assignment.adhd" "${CMAKE_CURRENT_LIST_DIR}/upload_validation.adhd" "${CMAKE_CURRENT_LIST_DIR}/capture/opcode_mapping.adhd" "${CMAKE_CURRENT_LIST_DIR}/telemetry_queue_class.adhd" "${CMAKE_CURRENT_LIST_DIR}/packet_runtime_names.adhd" "${CMAKE_CURRENT_LIST_DIR}/compiler_analysis_types.adhd" "${CMAKE_CURRENT_LIST_DIR}/packet_runtime_statistics.adhd" "${CMAKE_CURRENT_LIST_DIR}/task_graph_types.adhd" "${CMAKE_CURRENT_LIST_DIR}/capture/command_ir_value_types.adhd" "${CMAKE_CURRENT_LIST_DIR}/compiler_status_types.adhd")
target_sources(nwb_gpu_task PRIVATE
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
    "${CMAKE_CURRENT_LIST_DIR}/capture/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/capture/*.cc"
    "${CMAKE_CURRENT_LIST_DIR}/capture/*.cxx"
    "${CMAKE_CURRENT_LIST_DIR}/capture/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/capture/*.h"
    "${CMAKE_CURRENT_LIST_DIR}/capture/*.hh"
    "${CMAKE_CURRENT_LIST_DIR}/capture/*.hxx"
    "${CMAKE_CURRENT_LIST_DIR}/capture/*.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/capture/*.inl"
    "${CMAKE_CURRENT_LIST_DIR}/capture/*.ipp"
    "${CMAKE_CURRENT_LIST_DIR}/capture/*.ixx"
)

target_link_libraries(nwb_gpu_task PUBLIC nwb_graphics_backend nwb_cpu_task nwb_telemetry)

