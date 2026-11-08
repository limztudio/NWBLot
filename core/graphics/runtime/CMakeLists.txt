nwb_declare_static_library(nwb_graphics_frame_graph_names)
nwb_target_adhd_sources(nwb_graphics_frame_graph_names NAMESPACE "NWB::Core::GraphicsFrameGraphNodes" SOURCES "${PROJECT_SOURCE_DIR}/core/graphics/frame_graph_nodes.adhd")

nwb_declare_static_library(nwb_graphics_runtime_values)
nwb_target_adhd_sources(nwb_graphics_runtime_values NAMESPACE "NWB::Core::GraphicsRuntimeValues" SOURCES "${CMAKE_CURRENT_LIST_DIR}/runtime_values.adhd")

nwb_declare_static_library(nwb_graphics)
nwb_target_adhd_sources(nwb_graphics NAMESPACE "NWB::Core::GraphicsProfileScope" SOURCES "${CMAKE_CURRENT_LIST_DIR}/profile_names.adhd")
target_sources(nwb_graphics PRIVATE
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

target_link_libraries(nwb_graphics PRIVATE nwb_graphics_internal_names nwb_graphics_runtime_values)
target_link_libraries(nwb_graphics PUBLIC nwb_graphics_backend nwb_gpu_task nwb_graphics_frame_graph_names)

