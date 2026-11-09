nwb_declare_static_library(nwb_assets_graphics_types)
nwb_target_adhd_sources(nwb_assets_graphics_types
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/bvh_subsystem.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/components.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/cook_types.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/csg_subsystem.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/deferred_subsystem.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/gi_subsystem.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/graphics_component_binary_codec.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/mesh_subsystem.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/reflection_subsystem.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/refractive_subsystem.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/renderer_subsystem.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/shadow_subsystem.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/transparent_subsystem.adhd"
)
target_link_libraries(nwb_assets_graphics_types PUBLIC nwb_assets nwb_ecs nwb_ecs_scene nwb_assets_scene_types nwb_assets_csg nwb_assets_sampler_types nwb_assets_shader_types nwb_ecs_mesh_components nwb_ecs_model_components nwb_ecs_render_components)

nwb_declare_static_library(nwb_assets_graphics)
nwb_target_adhd_sources(nwb_assets_graphics
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/runtime_components.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/bindless/runtime_abi.adhd"
)
target_link_libraries(nwb_assets_graphics PUBLIC
    nwb_assets_mesh nwb_assets_skeleton nwb_assets_model nwb_assets_subsystem nwb_assets_scene nwb_assets_material nwb_assets_shader nwb_assets_texture nwb_assets_sampler nwb_ecs_mesh nwb_ecs_model nwb_ecs_render_components nwb_assets_graphics_types
)

if(NWB_BUILD_PIPELINE)
    nwb_declare_static_library(nwb_assets_graphics_gather)
    nwb_target_adhd_sources(nwb_assets_graphics_gather
        NAMESPACE "NWB::Impl"
        SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/gather.adhd"
    )
    target_link_libraries(nwb_assets_graphics_gather PUBLIC nwb_assets PRIVATE nwb_shader_archive)

    nwb_declare_static_library(nwb_assets_graphics_cook)
    nwb_target_adhd_sources(nwb_assets_graphics_cook
        NAMESPACE "NWB::Impl"
        SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/runtime_components.adhd"
            "${CMAKE_CURRENT_LIST_DIR}/cook_components.adhd"
            "${CMAKE_CURRENT_LIST_DIR}/csg_shader_variants.adhd"
            "${CMAKE_CURRENT_LIST_DIR}/shader_cook_plan.adhd"
    )
    target_link_libraries(nwb_assets_graphics_cook PUBLIC
        nwb_assets nwb_assets_subsystem_cook nwb_assets_scene_cook nwb_assets_mesh_cook nwb_assets_skeleton_cook nwb_assets_model_cook nwb_assets_material_cook nwb_ecs_mesh_components nwb_ecs_model_components nwb_ecs_render_components nwb_assets_graphics_types
    )
    target_link_libraries(nwb_assets_graphics_cook PRIVATE nwb_assets_volume_cook nwb_assets_csg_cook nwb_assets_shader_cook nwb_shader_archive nwb_filesystem)
    target_compile_definitions(nwb_assets_graphics_cook PRIVATE NWB_COOK=1)

    nwb_declare_static_library(nwb_assets_graphics_volume_entry)
    nwb_target_adhd_sources(nwb_assets_graphics_volume_entry
        NAMESPACE "NWB::Impl"
        SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/volume_entry.adhd"
    )
    target_link_libraries(nwb_assets_graphics_volume_entry PRIVATE nwb_assets_graphics_cook nwb_assets_subsystem_cook nwb_assets_scene_cook nwb_assets_cook)
    target_compile_definitions(nwb_assets_graphics_volume_entry PRIVATE NWB_COOK=1)
endif()
