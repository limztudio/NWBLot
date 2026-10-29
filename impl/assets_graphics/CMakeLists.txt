nwb_declare_static_library(nwb_assets_graphics_types)
nwb_target_adhd_sources(nwb_assets_graphics_types
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/*.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/*/*.adhd"
)
target_link_libraries(nwb_assets_graphics_types PUBLIC nwb_assets nwb_ecs nwb_ecs_scene nwb_assets_scene_types nwb_assets_csg nwb_assets_sampler_types nwb_assets_shader_types nwb_ecs_mesh nwb_ecs_model nwb_ecs_render_components)

nwb_declare_static_library(nwb_assets_graphics)
target_sources(nwb_assets_graphics PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/runtime*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.h"
    "${CMAKE_CURRENT_LIST_DIR}/*.hh"
    "${CMAKE_CURRENT_LIST_DIR}/*.hxx"
    "${CMAKE_CURRENT_LIST_DIR}/*.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.inl"
    "${CMAKE_CURRENT_LIST_DIR}/*.ipp"
    "${CMAKE_CURRENT_LIST_DIR}/bindless/*.h"
    "${CMAKE_CURRENT_LIST_DIR}/bindless/*.hh"
    "${CMAKE_CURRENT_LIST_DIR}/bindless/*.hxx"
    "${CMAKE_CURRENT_LIST_DIR}/bindless/*.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/bindless/*.inl"
    "${CMAKE_CURRENT_LIST_DIR}/bindless/*.ipp"
)
target_link_libraries(nwb_assets_graphics PUBLIC nwb_assets_mesh nwb_assets_skeleton nwb_assets_model nwb_assets_subsystem nwb_assets_scene nwb_assets_material nwb_assets_shader nwb_assets_texture nwb_assets_sampler nwb_ecs_mesh nwb_ecs_model nwb_ecs_render_components nwb_assets_graphics_types)

if(NWB_BUILD_PIPELINE)
    nwb_declare_static_library(nwb_assets_graphics_gather)
    target_sources(nwb_assets_graphics_gather PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/gather.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/gather.h"
    )
    target_link_libraries(nwb_assets_graphics_gather PUBLIC nwb_assets PRIVATE nwb_shader_archive)

    nwb_declare_static_library(nwb_assets_graphics_cook)
    target_sources(nwb_assets_graphics_cook PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/runtime*.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/cook_components.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/csg_shader_variants.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/material_validation.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/shader_*.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/volume_prepare.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/*.h"
        "${CMAKE_CURRENT_LIST_DIR}/*.hh"
        "${CMAKE_CURRENT_LIST_DIR}/*.hxx"
        "${CMAKE_CURRENT_LIST_DIR}/*.hpp"
        "${CMAKE_CURRENT_LIST_DIR}/*.inl"
        "${CMAKE_CURRENT_LIST_DIR}/*.ipp"
    )
    target_link_libraries(nwb_assets_graphics_cook PUBLIC nwb_assets nwb_assets_subsystem_cook nwb_assets_scene_cook nwb_assets_mesh nwb_assets_skeleton nwb_assets_model nwb_assets_material nwb_ecs_mesh nwb_ecs_model nwb_ecs_render_components nwb_assets_graphics_types)
    target_link_libraries(nwb_assets_graphics_cook PRIVATE nwb_assets_volume_cook nwb_assets_csg_cook nwb_assets_material_cook nwb_assets_shader_cook nwb_shader_archive nwb_filesystem)
    target_compile_definitions(nwb_assets_graphics_cook PRIVATE NWB_COOK=1)

    nwb_declare_static_library(nwb_assets_graphics_volume_entry)
    target_sources(nwb_assets_graphics_volume_entry PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/volume_entry.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/cook_components.h"
    )
    target_link_libraries(nwb_assets_graphics_volume_entry PRIVATE nwb_assets_graphics_cook nwb_assets_subsystem_cook nwb_assets_scene_cook nwb_assets_cook)
    target_compile_definitions(nwb_assets_graphics_volume_entry PRIVATE NWB_COOK=1)
endif()
