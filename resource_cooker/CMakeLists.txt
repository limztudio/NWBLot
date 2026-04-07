nwb_declare_executable(nwb_resource_cooker)
target_sources(nwb_resource_cooker PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/main.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/resource_cooker.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/resource_cooker_command_line.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/resource_cooker.h"
)
target_link_libraries(nwb_resource_cooker PRIVATE
    nwb_assets
    nwb_alloc
    nwb_filesystem
    nwb_logclient
    nwb_common
    nwb::tbb
    nwb_metascript
    nwb_graphics
    nwb_graphics_cook
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,nwb_assets_graphics_cook>"
    nwb::cli11_headers
)
target_compile_definitions(nwb_resource_cooker PRIVATE __TBB_NO_IMPLICIT_LINKAGE=1 __TBBMALLOC_NO_IMPLICIT_LINKAGE=1)
