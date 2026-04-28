add_library(nwb_vendor_imgui STATIC
    "${CMAKE_CURRENT_LIST_DIR}/imgui.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/imgui_demo.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/imgui_draw.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/imgui_tables.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/imgui_widgets.cpp"
)
add_library(nwb::imgui ALIAS nwb_vendor_imgui)

target_include_directories(nwb_vendor_imgui SYSTEM PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}"
)

nwb_disable_vendor_warnings(nwb_vendor_imgui)
