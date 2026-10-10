nwb_declare_static_library(nwb_ui_images)
target_sources(nwb_ui_images PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/skin_region_geometry.h"
    "${CMAKE_CURRENT_LIST_DIR}/image_source.h"
    "${CMAKE_CURRENT_LIST_DIR}/image_source.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/image_loader.h"
    "${CMAKE_CURRENT_LIST_DIR}/image_loader.cpp"
)
target_link_libraries(nwb_ui_images PUBLIC nwb_assets_texture nwb_assets_ui_skin)
