nwb_declare_executable(nwb_logserver)
target_sources(nwb_logserver PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/frame.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/main.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/pch.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/server.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/frame.h"
    "${CMAKE_CURRENT_LIST_DIR}/pch.h"
    "${CMAKE_CURRENT_LIST_DIR}/server.h"
)
if(WIN32)
    target_sources(nwb_logserver PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/frame_win.cpp"
    )
    set_target_properties(nwb_logserver PROPERTIES WIN32_EXECUTABLE ON)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(nwb_logserver PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/frame_linux.cpp"
    )
endif()
target_link_libraries(nwb_logserver PRIVATE nwb::microhttpd nwb_alloc nwb_common nwb::cli11_headers)

