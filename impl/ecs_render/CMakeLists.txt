nwb_declare_interface_library(nwb_ecs_render_components)
target_sources(nwb_ecs_render_components INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/components.h"
    "${CMAKE_CURRENT_LIST_DIR}/material_instance.h"
)
target_link_libraries(nwb_ecs_render_components INTERFACE
    nwb_assets
    nwb_assets_material
    nwb_ecs
)

nwb_declare_static_library(nwb_ecs_render)
target_sources(nwb_ecs_render PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/system.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/avboit_pass.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/avboit_resources.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/avboit_targets.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/csg_cap_source.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/csg_caps.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/csg_plane_cap_builder.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/csg_resources.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/deferred_composite.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/deferred_frame.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/deferred_lighting.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/material_pipeline.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/material_pass_draw.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/material_pass_resources.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/material_pass.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/mesh_bindings.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/mesh_resources.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/material_surface.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/material_instance.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/shader_loading.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/renderer_state.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/module.h"
    "${CMAKE_CURRENT_LIST_DIR}/avboit.h"
    "${CMAKE_CURRENT_LIST_DIR}/system.h"
    "${CMAKE_CURRENT_LIST_DIR}/avboit_private.h"
    "${CMAKE_CURRENT_LIST_DIR}/csg_cap_source.h"
    "${CMAKE_CURRENT_LIST_DIR}/csg_plane_cap_builder.h"
    "${CMAKE_CURRENT_LIST_DIR}/material_shader_variants_private.h"
    "${CMAKE_CURRENT_LIST_DIR}/mesh_view_private.h"
    "${CMAKE_CURRENT_LIST_DIR}/renderer_capacity_private.h"
    "${CMAKE_CURRENT_LIST_DIR}/renderer_csg_private.h"
    "${CMAKE_CURRENT_LIST_DIR}/renderer_private.h"
    "${CMAKE_CURRENT_LIST_DIR}/renderer_state.h"
    "${CMAKE_CURRENT_LIST_DIR}/timing_names.h"
    "${CMAKE_CURRENT_LIST_DIR}/renderer_types.h"
)
target_link_libraries(nwb_ecs_render PUBLIC
    nwb_ecs_render_components
    nwb_ecs
    nwb_ecs_mesh
    nwb_ecs_mesh_runtime
    nwb_graphics
    nwb_assets_material
)
target_link_libraries(nwb_ecs_render PRIVATE
    nwb_assets_mesh
    nwb_assets_shader_loader
    nwb_ecs_csg
    nwb_ecs_scene
)

