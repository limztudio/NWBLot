nwb_declare_static_library(nwb_ecs_render_components)
target_sources(nwb_ecs_render_components PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/material/material_instance.h"
)
nwb_target_adhd_sources(nwb_ecs_render_components
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/*.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/*/*.adhd"
    DEPENDS
        "${CMAKE_CURRENT_LIST_DIR}/*/*.adhd"
        "${PROJECT_SOURCE_DIR}/impl/assets_material/*.adhd"
)
target_link_libraries(nwb_ecs_render_components PUBLIC nwb_assets nwb_assets_material nwb_ecs)

nwb_declare_static_library(nwb_ecs_render)
target_sources(nwb_ecs_render PRIVATE
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
    "${CMAKE_CURRENT_LIST_DIR}/*/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/*/*.cc"
    "${CMAKE_CURRENT_LIST_DIR}/*/*.cxx"
    "${CMAKE_CURRENT_LIST_DIR}/*/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/*/*.h"
    "${CMAKE_CURRENT_LIST_DIR}/*/*.hh"
    "${CMAKE_CURRENT_LIST_DIR}/*/*.hxx"
    "${CMAKE_CURRENT_LIST_DIR}/*/*.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/*/*.inl"
    "${CMAKE_CURRENT_LIST_DIR}/*/*.ipp"
    "${CMAKE_CURRENT_LIST_DIR}/*/*.ixx"
)
target_link_libraries(nwb_ecs_render PUBLIC nwb_ecs_render_components nwb_ecs nwb_ecs_mesh nwb_graphics nwb_assets_material nwb_ecs_csg nwb_ecs_scene nwb_assets_scene nwb_assets_graphics_types)
target_link_libraries(nwb_ecs_render PRIVATE nwb_assets_mesh nwb_assets_shader_loader nwb_assets_texture_loader nwb_assets_sampler_loader)
