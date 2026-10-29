nwb_declare_static_library(nwb_assets_graphics_types)
nwb_target_adhd_sources(nwb_assets_graphics_types
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/components.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/bvh/subsystem.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/csg/subsystem.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/deferred/subsystem.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/gi/subsystem.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/mesh/subsystem.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/reflection/subsystem.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/refractive/subsystem.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/renderer/subsystem.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/shadow/subsystem.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/transparent/subsystem.adhd"
)
target_link_libraries(nwb_assets_graphics_types PUBLIC nwb_assets nwb_ecs nwb_ecs_scene nwb_assets_scene_types nwb_ecs_mesh nwb_ecs_model nwb_ecs_render_components)

nwb_declare_static_library(nwb_assets_graphics)
target_sources(nwb_assets_graphics PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/runtime.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/runtime_components.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/component_binary_payload.h"
    "${CMAKE_CURRENT_LIST_DIR}/bindless/runtime_abi.h"
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
        "${CMAKE_CURRENT_LIST_DIR}/runtime.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/runtime_components.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/cook_components.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/cook_components.h"
        "${CMAKE_CURRENT_LIST_DIR}/component_binary_payload.h"
        "${CMAKE_CURRENT_LIST_DIR}/csg_shader_variants.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/csg_shader_variants.h"
        "${CMAKE_CURRENT_LIST_DIR}/material_validation.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/material_validation.h"
        "${CMAKE_CURRENT_LIST_DIR}/shader_cook_plan.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/shader_cook_plan.h"
        "${CMAKE_CURRENT_LIST_DIR}/shader_stage_key.h"
        "${CMAKE_CURRENT_LIST_DIR}/shader_volume_writer.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/shader_volume_writer.h"
        "${CMAKE_CURRENT_LIST_DIR}/volume_prepare.cpp"
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
