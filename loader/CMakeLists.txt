nwb_declare_static_library(loader)
target_sources(loader PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/main.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/project_entry.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/pch.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/pch.h"
    "${CMAKE_CURRENT_LIST_DIR}/project_entry.h"
)
target_link_libraries(loader PUBLIC alloc frame logclient nwb::cli11_headers)
