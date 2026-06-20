nwb_declare_static_library(nwb_common)
target_sources(nwb_common PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/module.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/log.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/aftermath_runtime.h"
    "${CMAKE_CURRENT_LIST_DIR}/arena_names.h"
    "${CMAKE_CURRENT_LIST_DIR}/application_entry.h"
    "${CMAKE_CURRENT_LIST_DIR}/command_line.h"
    "${CMAKE_CURRENT_LIST_DIR}/module.h"
    "${CMAKE_CURRENT_LIST_DIR}/log.h"
    "${CMAKE_CURRENT_LIST_DIR}/win32_message_loop.h"
)
target_link_libraries(nwb_common PUBLIC nwb_alloc nwb::tbb)
