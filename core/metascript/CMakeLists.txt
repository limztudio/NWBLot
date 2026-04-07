nwb_declare_static_library(metascript)
target_sources(metascript PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/value.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lexer.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/parser.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/value.h"
    "${CMAKE_CURRENT_LIST_DIR}/lexer.h"
    "${CMAKE_CURRENT_LIST_DIR}/parser.h"
)
target_link_libraries(metascript PUBLIC nwb::tbb)
