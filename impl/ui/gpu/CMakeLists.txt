nwb_declare_static_library(nwb_ui_gpu)
target_sources(nwb_ui_gpu PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/renderer.h"
    "${CMAKE_CURRENT_LIST_DIR}/renderer_internal.h"
    "${CMAKE_CURRENT_LIST_DIR}/renderer.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/renderer_resources.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/renderer_glyph_resources.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/renderer_image_cache.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/renderer_sdf_resources.h"
    "${CMAKE_CURRENT_LIST_DIR}/renderer_sdf_resources.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/renderer_texture_resources.h"
    "${CMAKE_CURRENT_LIST_DIR}/renderer_texture_resources.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/renderer_graph.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/renderer_command_ir.h"
    "${CMAKE_CURRENT_LIST_DIR}/renderer_command_ir.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/renderer_raster.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/renderer_standalone.cpp"
)
target_link_libraries(nwb_ui_gpu PUBLIC nwb_ui nwb_graphics nwb_assets_texture_loader)
target_link_libraries(nwb_ui_gpu PRIVATE nwb_assets_shader_loader)
