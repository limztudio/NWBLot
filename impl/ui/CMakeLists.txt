nwb_declare_static_library(nwb_ui)
target_sources(nwb_ui PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/global.h"
    "${CMAKE_CURRENT_LIST_DIR}/paint.h"
    "${CMAKE_CURRENT_LIST_DIR}/paint.cpp"
)
target_link_libraries(nwb_ui PUBLIC nwb_assets_ui_skin)

