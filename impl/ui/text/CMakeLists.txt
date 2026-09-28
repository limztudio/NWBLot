nwb_declare_static_library(nwb_ui_text)
target_sources(nwb_ui_text PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/types.h"
    "${CMAKE_CURRENT_LIST_DIR}/types.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/font.h"
    "${CMAKE_CURRENT_LIST_DIR}/font.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/shaper.h"
    "${CMAKE_CURRENT_LIST_DIR}/shaper.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/layout.h"
    "${CMAKE_CURRENT_LIST_DIR}/layout.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/atlas.h"
    "${CMAKE_CURRENT_LIST_DIR}/atlas.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/baked_atlas.h"
    "${CMAKE_CURRENT_LIST_DIR}/baked_atlas.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/service.h"
    "${CMAKE_CURRENT_LIST_DIR}/service.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/service_paint.cpp"
)
target_link_libraries(nwb_ui_text PUBLIC nwb_ui nwb_assets_font_atlas PRIVATE nwb::freetype nwb::harfbuzz)

