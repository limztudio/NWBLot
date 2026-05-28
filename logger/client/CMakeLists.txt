nwb_declare_static_library(nwb_logclient)
target_sources(nwb_logclient PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/module.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/logger.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/module.h"
    "${CMAKE_CURRENT_LIST_DIR}/logger.h"
)
target_link_libraries(nwb_logclient PUBLIC nwb_common)
target_link_libraries(nwb_logclient PRIVATE nwb::curl)
