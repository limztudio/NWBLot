nwb_declare_static_library(nwb_ui_widgets)
target_sources(nwb_ui_widgets PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/style.h"
    "${CMAKE_CURRENT_LIST_DIR}/controls_paint.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/edit_box.h"
    "${CMAKE_CURRENT_LIST_DIR}/edit_box_state.h"
    "${CMAKE_CURRENT_LIST_DIR}/edit_box_layout.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/edit_box_paint.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/builder_edit_box.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/window.h"
    "${CMAKE_CURRENT_LIST_DIR}/window_behavior.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/window_layout.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/window_paint.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/builder_window.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/builder_separator.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../builder.h"
    "${CMAKE_CURRENT_LIST_DIR}/../builder.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/label.h"
    "${CMAKE_CURRENT_LIST_DIR}/label.cpp"
)
target_link_libraries(nwb_ui_widgets PUBLIC nwb_ui_text nwb_ui_edit)

