nwb_declare_static_library(nwb_ecs_scene)
target_sources(nwb_ecs_scene PRIVATE
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
nwb_target_adhd_sources(nwb_ecs_scene
    NAMESPACE "NWB::Impl::Scene"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/*.adhd"
)
target_link_libraries(nwb_ecs_scene PUBLIC nwb_ecs)
