nwb_declare_static_library(nwb_ecs_mesh_skinning)
target_sources(nwb_ecs_mesh_skinning PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/runtime_cache.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/runtime_cache_resources.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/runtime_cache_source.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/system.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/system_live_state.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/graph_resource_uses.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/graph_resource_uses.h"
    "${CMAKE_CURRENT_LIST_DIR}/graph_dispatch_plan.h"
    "${CMAKE_CURRENT_LIST_DIR}/live_state_buffers.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/live_state_buffers.h"
    "${CMAKE_CURRENT_LIST_DIR}/pipeline.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/resources.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/skinning.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/runtime_cache.h"
    "${CMAKE_CURRENT_LIST_DIR}/runtime_instance.h"
    "${CMAKE_CURRENT_LIST_DIR}/runtime_mesh_liveness.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/runtime_mesh_liveness.h"
    "${CMAKE_CURRENT_LIST_DIR}/resource_names.h"
    "${CMAKE_CURRENT_LIST_DIR}/skin_payload.h"
    "${CMAKE_CURRENT_LIST_DIR}/submission_state.h"
    "${CMAKE_CURRENT_LIST_DIR}/system.h"
    "${CMAKE_CURRENT_LIST_DIR}/module.h"
    "${CMAKE_CURRENT_LIST_DIR}/arena_names.h"
    "${CMAKE_CURRENT_LIST_DIR}/timing_names.h"
)
target_link_libraries(nwb_ecs_mesh_skinning PUBLIC
    nwb_ecs_mesh
    nwb_ecs_skeleton
    nwb_graphics
    nwb_assets_mesh
)
target_link_libraries(nwb_ecs_mesh_skinning PRIVATE
    nwb_assets_shader_loader
)

