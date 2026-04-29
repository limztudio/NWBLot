nwb_declare_executable(nwb_fbx_to_nwb)
target_sources(nwb_fbx_to_nwb PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/command_line.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/fbx_import.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/fbx_to_nwb_common.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/main.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/nwb_geometry_writer.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/fbx_to_nwb.h"
    "${CMAKE_CURRENT_LIST_DIR}/global.h"
)
target_link_libraries(nwb_fbx_to_nwb PRIVATE
    nwb_common
    nwb_alloc
    nwb::ufbx
    nwb::cli11_headers
)
target_compile_definitions(nwb_fbx_to_nwb PRIVATE __TBB_NO_IMPLICIT_LINKAGE=1 __TBBMALLOC_NO_IMPLICIT_LINKAGE=1)
