nwb_declare_executable(resource_cooker)
target_sources(resource_cooker PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/main.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/resource_cooker.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/resource_cooker_command_line.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/resource_cooker.h"
)
target_link_libraries(resource_cooker PRIVATE
    assets
    alloc
    filesystem
    logclient
    common
    nwb::tbb
    metascript
    graphics
    graphics_cook
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,assets_graphics_cook>"
    nwb::cli11_headers
)
target_compile_definitions(resource_cooker PRIVATE __TBB_NO_IMPLICIT_LINKAGE=1 __TBBMALLOC_NO_IMPLICIT_LINKAGE=1)
