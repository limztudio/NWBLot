nwb_declare_static_library(nwb_metascript)
target_sources(nwb_metascript PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/value.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lexer.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/parser.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/parser_cursor.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/operation_validator.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/expression_parser.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/statement_parser.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/value.h"
    "${CMAKE_CURRENT_LIST_DIR}/lexer.h"
    "${CMAKE_CURRENT_LIST_DIR}/parser.h"
    "${CMAKE_CURRENT_LIST_DIR}/parser_cursor.h"
    "${CMAKE_CURRENT_LIST_DIR}/operation_validator.h"
    "${CMAKE_CURRENT_LIST_DIR}/expression_parser.h"
    "${CMAKE_CURRENT_LIST_DIR}/statement_parser.h"
    "${CMAKE_CURRENT_LIST_DIR}/arena_names.h"
)
target_link_libraries(nwb_metascript PUBLIC nwb_alloc nwb::tbb)
