nwb_declare_static_library(nwb_ecs_model)
target_sources(nwb_ecs_model PRIVATE
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
nwb_target_adhd_sources(nwb_ecs_model
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/*.adhd"
    DEPENDS
        "${PROJECT_SOURCE_DIR}/impl/assets_skeleton/*.adhd"
)
target_link_libraries(nwb_ecs_model PUBLIC nwb_ecs nwb_assets_skeleton_types)
target_link_libraries(nwb_ecs_model PRIVATE nwb_assets_model nwb_ecs_mesh nwb_ecs_scene nwb_ecs_skeleton)
