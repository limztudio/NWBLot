nwb_declare_static_library(nwb_gpu_task)
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
