set(_nwb_ecs_render_component_adhd_sources
    "${CMAKE_CURRENT_LIST_DIR}/components.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/avboit/geometry_types.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/avboit/graph_types.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/avboit/phase_identities.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/avboit/types.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/caustic/caustic_emission_target_builder.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/caustic/caustic_light_selector.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/caustic/graph_types.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/caustic/pipeline_types.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/caustic/push_constants.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/caustic/settings.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/csg/clip_bounds.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/csg/csg_snapshot_identity_builder.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/csg/cutter_gpu_data.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/csg/graph_types.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/csg/types.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/csg/work_region.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/deferred/deferred_bindless_slots_builder.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/deferred/graph_types.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/deferred/import_types.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/deferred/presentation_settings.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/deferred/scene_shading_upload_tracker.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/execute/frame_diagnostics.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/execute/types.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/gi/lifecycle_types.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/gi/quality_settings.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/kernel/timing_policy.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/material/csg_shader_variant_builder.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/material/instance_override_fields.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/material/instance_typed_ranges.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/material/material_instance.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/material/material_instance_override_applier.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/material/material_surface_builder.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/material/pipeline_enums.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/material/pipeline_types.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/mesh/instance_gpu_data.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/mesh/mesh_acceleration_state_policy.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/mesh/mesh_geometry_buffer_policy.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/mesh/mesh_view_state_policy.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/optics/coincident_volume_types.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/optics/graph_types.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/optics/optical_bounds_evaluator.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/optics/optical_scene_binary_codec.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/optics/optical_scene_identity_builder.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/optics/types.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/raytrace/graph_types.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/raytrace/push_constants.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/raytrace/raytracing_build_policy.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/raytrace/raytracing_diagnostics.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/raytrace/raytracing_scene_identity_builder.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/raytrace/refractive_bounds_isolation.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/raytrace/scene_bvh.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/raytrace/scene_bvh_build.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/raytrace/scene_bvh_upload_policy.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/raytrace/types.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/reflection/feedback_planning.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/reflection/frame_parameters.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/reflection/history_planning.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/reflection/push_constants.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/reflection/settings.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/reflection/statistics_types.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/reflection/types.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/refraction/types.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/shadow/light_space_capture_identity_builder.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/shadow/light_space_csg_context_packer.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/shadow/light_space_plan.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/shadow/light_space_settings.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/shadow/light_space_shadow_diagnostics.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/shadow/light_space_types.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/shadow/push_constants.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/shadow/push_constants_temporal.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/shadow/push_constants_types.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/shadow/quality_settings.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/shadow/shadow_material_context_policy.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/shadow/slot_selection.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/shadow/types.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/shared/frame_push_constants.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/shared/frame_types.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/shared/statistics_types.adhd"
)

nwb_declare_static_library(nwb_ecs_render_components)
nwb_target_adhd_sources(nwb_ecs_render_components
    NAMESPACE "NWB::Impl"
    SOURCES
        ${_nwb_ecs_render_component_adhd_sources}
)
target_link_libraries(nwb_ecs_render_components PUBLIC nwb_assets nwb_assets_material_types nwb_assets_mesh_types nwb_ecs nwb_ecs_csg nwb_ecs_scene nwb_gpu_task)

file(GLOB _nwb_ecs_render_adhd_sources CONFIGURE_DEPENDS LIST_DIRECTORIES false
    "${CMAKE_CURRENT_LIST_DIR}/*.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/*/*.adhd"
)
list(REMOVE_ITEM _nwb_ecs_render_adhd_sources ${_nwb_ecs_render_component_adhd_sources})

nwb_declare_static_library(nwb_ecs_render)
nwb_target_adhd_sources(nwb_ecs_render
    NAMESPACE "NWB::Impl"
    SOURCES
        ${_nwb_ecs_render_adhd_sources}
)
unset(_nwb_ecs_render_component_adhd_sources)
unset(_nwb_ecs_render_adhd_sources)
target_link_libraries(nwb_ecs_render PUBLIC nwb_ecs_render_components nwb_ecs nwb_ecs_mesh nwb_graphics nwb_assets_material nwb_ecs_csg nwb_ecs_scene nwb_assets_scene nwb_assets_graphics_types)
target_link_libraries(nwb_ecs_render PRIVATE nwb_assets_mesh nwb_assets_shader_loader nwb_assets_texture_loader nwb_assets_texture nwb_assets_sampler_loader)
