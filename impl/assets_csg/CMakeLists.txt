nwb_declare_static_library(nwb_assets_csg)
target_sources(nwb_assets_csg PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/asset.h"
    "${CMAKE_CURRENT_LIST_DIR}/shape_id.h"
)
nwb_target_adhd_sources(nwb_assets_csg
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/*.adhd"
)
target_link_libraries(nwb_assets_csg PUBLIC nwb_assets)

if(NWB_BUILD_PIPELINE)
    nwb_declare_static_library(nwb_assets_csg_cook)
    target_sources(nwb_assets_csg_cook PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/cook.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/*.h"
        "${CMAKE_CURRENT_LIST_DIR}/*.hh"
        "${CMAKE_CURRENT_LIST_DIR}/*.hxx"
        "${CMAKE_CURRENT_LIST_DIR}/*.hpp"
        "${CMAKE_CURRENT_LIST_DIR}/*.inl"
        "${CMAKE_CURRENT_LIST_DIR}/*.ipp"
    )
    target_link_libraries(nwb_assets_csg_cook PUBLIC nwb_assets nwb_assets_csg nwb_metascript nwb_filesystem)
    target_compile_definitions(nwb_assets_csg_cook PRIVATE NWB_COOK=1)

    nwb_declare_static_library(nwb_assets_csg_volume_entry)
    target_sources(nwb_assets_csg_volume_entry PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/volume_entry.cpp"
    )
    target_link_libraries(nwb_assets_csg_volume_entry PRIVATE nwb_assets_csg_cook nwb_assets_cook)
    target_compile_definitions(nwb_assets_csg_volume_entry PRIVATE NWB_COOK=1)
endif()
