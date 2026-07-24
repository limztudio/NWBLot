nwb_declare_interface_library(nwb_ecs_model_renderer)
target_sources(nwb_ecs_model_renderer INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/model_renderer.h"
)
target_link_libraries(nwb_ecs_model_renderer INTERFACE
    nwb_ecs_model
    nwb_ecs_render_components
)
