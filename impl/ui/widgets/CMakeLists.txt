nwb_declare_static_library(nwb_ui_widgets)
target_sources(nwb_ui_widgets PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/style.h"
    "${CMAKE_CURRENT_LIST_DIR}/controls_paint.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../builder.h"
    "${CMAKE_CURRENT_LIST_DIR}/../builder.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/label.h"
    "${CMAKE_CURRENT_LIST_DIR}/label.cpp"
)
target_link_libraries(nwb_ui_widgets PUBLIC nwb_ui_text)

