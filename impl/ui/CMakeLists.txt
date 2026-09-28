nwb_declare_static_library(nwb_ui)
target_sources(nwb_ui PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/global.h"
    "${CMAKE_CURRENT_LIST_DIR}/paint.h"
    "${CMAKE_CURRENT_LIST_DIR}/paint.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/paint_glyph.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/text/glyph_page.h"
    "${CMAKE_CURRENT_LIST_DIR}/text/glyph_page.cpp"
)
target_link_libraries(nwb_ui PUBLIC nwb_assets_ui_skin nwb_assets_font)

add_subdirectory(text)
add_subdirectory(widgets)
add_subdirectory(gpu)

