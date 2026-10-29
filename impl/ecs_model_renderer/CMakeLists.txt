nwb_declare_interface_library(nwb_ecs_model_renderer)
target_sources(nwb_ecs_model_renderer INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/*.h"
    "${CMAKE_CURRENT_LIST_DIR}/*.hh"
    "${CMAKE_CURRENT_LIST_DIR}/*.hxx"
    "${CMAKE_CURRENT_LIST_DIR}/*.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.inl"
    "${CMAKE_CURRENT_LIST_DIR}/*.ipp"
)
target_link_libraries(nwb_ecs_model_renderer INTERFACE nwb_ecs_model nwb_ecs_render_components)
