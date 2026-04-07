nwb_declare_executable(logserver)
target_sources(logserver PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/frame.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/frame_win.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/main.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/pch.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/server.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/frame.h"
    "${CMAKE_CURRENT_LIST_DIR}/pch.h"
    "${CMAKE_CURRENT_LIST_DIR}/server.h"
)
target_link_libraries(logserver PRIVATE nwb::microhttpd alloc common nwb::cli11_headers)
