nwb_declare_static_library(nwb_assets_skeleton_types)
nwb_target_adhd_sources(nwb_assets_skeleton_types
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/*.adhd"
)
target_link_libraries(nwb_assets_skeleton_types PUBLIC nwb_alloc nwb_ecs)

nwb_declare_static_library(nwb_assets_skeleton)
target_sources(nwb_assets_skeleton PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/runtime.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.h"
    "${CMAKE_CURRENT_LIST_DIR}/*.hh"
    "${CMAKE_CURRENT_LIST_DIR}/*.hxx"
    "${CMAKE_CURRENT_LIST_DIR}/*.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.inl"
    "${CMAKE_CURRENT_LIST_DIR}/*.ipp"
)
target_link_libraries(nwb_assets_skeleton PUBLIC nwb_assets nwb_assets_skeleton_types)

if(NWB_BUILD_PIPELINE)
    nwb_declare_static_library(nwb_assets_skeleton_cook)
    target_sources(nwb_assets_skeleton_cook PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/cook.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/runtime.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/*.h"
        "${CMAKE_CURRENT_LIST_DIR}/*.hh"
        "${CMAKE_CURRENT_LIST_DIR}/*.hxx"
        "${CMAKE_CURRENT_LIST_DIR}/*.hpp"
        "${CMAKE_CURRENT_LIST_DIR}/*.inl"
        "${CMAKE_CURRENT_LIST_DIR}/*.ipp"
    )
    target_link_libraries(nwb_assets_skeleton_cook PUBLIC nwb_assets nwb_assets_skeleton_types)
    target_compile_definitions(nwb_assets_skeleton_cook PRIVATE NWB_COOK=1)

    nwb_declare_static_library(nwb_assets_skeleton_volume_entry)
    target_sources(nwb_assets_skeleton_volume_entry PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/volume_entry.cpp"
    )
    target_link_libraries(nwb_assets_skeleton_volume_entry PRIVATE nwb_assets_skeleton_cook nwb_assets_cook)
    target_compile_definitions(nwb_assets_skeleton_volume_entry PRIVATE NWB_COOK=1)
endif()
