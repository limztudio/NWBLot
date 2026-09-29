nwb_declare_static_library(nwb_ui)
target_sources(nwb_ui PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/id.h"
    "${CMAKE_CURRENT_LIST_DIR}/id.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/state/store.h"
    "${CMAKE_CURRENT_LIST_DIR}/state/store.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/context.h"
    "${CMAKE_CURRENT_LIST_DIR}/context.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/input/input.h"
    "${CMAKE_CURRENT_LIST_DIR}/input/router.h"
    "${CMAKE_CURRENT_LIST_DIR}/input/router.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/input/router_pointer.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/input/router_gesture.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/input/router_keyboard.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/layout/tree.h"
    "${CMAKE_CURRENT_LIST_DIR}/layout/tree.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/layout/measure.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/layout/arrange.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/global.h"
    "${CMAKE_CURRENT_LIST_DIR}/paint.h"
    "${CMAKE_CURRENT_LIST_DIR}/paint.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/paint_glyph.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/paint_images.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/paint_sdf.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/text/sdf_page.h"
    "${CMAKE_CURRENT_LIST_DIR}/text/sdf_page.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/text/glyph_page.h"
    "${CMAKE_CURRENT_LIST_DIR}/text/glyph_page.cpp"
)
target_link_libraries(nwb_ui PUBLIC nwb_assets_ui_skin nwb_assets_font nwb_assets_font_atlas)

add_subdirectory(edit)
add_subdirectory(text)
add_subdirectory(widgets)
add_subdirectory(gpu)

