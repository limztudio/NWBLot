nwb_declare_static_library(logclient)
target_sources(logclient PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/client.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/logger.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/client.h"
    "${CMAKE_CURRENT_LIST_DIR}/logger.h"
)
target_link_libraries(logclient PUBLIC nwb::curl)
