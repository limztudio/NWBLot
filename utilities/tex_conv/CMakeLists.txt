nwb_declare_executable(nwb_tex_conv)
target_sources(nwb_tex_conv PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/command_line.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/common.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/encode.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/main.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/output.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/global.h"
    "${CMAKE_CURRENT_LIST_DIR}/module.h"
)
target_link_libraries(nwb_tex_conv PRIVATE
    nwb_common
    nwb_alloc
    nwb_logclient
    nwb::basis_universal
    nwb::cli11_headers
)
