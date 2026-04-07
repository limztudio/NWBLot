nwb_declare_executable(nwb_logserver)
target_sources(nwb_logserver PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/frame.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/frame_win.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/main.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/pch.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/server.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/frame.h"
    "${CMAKE_CURRENT_LIST_DIR}/pch.h"
    "${CMAKE_CURRENT_LIST_DIR}/server.h"
)
target_link_libraries(nwb_logserver PRIVATE nwb::microhttpd nwb_alloc nwb_common nwb::cli11_headers)
