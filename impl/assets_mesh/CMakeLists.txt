nwb_declare_static_library(nwb_assets_mesh_types)
nwb_target_adhd_sources(nwb_assets_mesh_types
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/*.adhd"
    DEPENDS
        "${CMAKE_CURRENT_LIST_DIR}/*.adhd"
)
target_link_libraries(nwb_assets_mesh_types PUBLIC nwb_assets nwb_assets_skeleton_types)

set(NWB_ASSETS_MESH_RUNTIME_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/skin_runtime.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/runtime*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/meshlet_ref_validation.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.h"
    "${CMAKE_CURRENT_LIST_DIR}/*.hh"
    "${CMAKE_CURRENT_LIST_DIR}/*.hxx"
    "${CMAKE_CURRENT_LIST_DIR}/*.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.inl"
    "${CMAKE_CURRENT_LIST_DIR}/*.ipp"
)

nwb_declare_static_library(nwb_assets_mesh)
target_sources(nwb_assets_mesh PRIVATE
    ${NWB_ASSETS_MESH_RUNTIME_SOURCES}
)
target_link_libraries(nwb_assets_mesh PUBLIC nwb_assets nwb_assets_subsystem nwb_assets_skeleton nwb_mesh nwb_assets_skeleton_types nwb_assets_mesh_types)

if(NWB_BUILD_PIPELINE)
    nwb_declare_static_library(nwb_assets_mesh_cook)
    target_sources(nwb_assets_mesh_cook PRIVATE
        ${NWB_ASSETS_MESH_RUNTIME_SOURCES}
        "${CMAKE_CURRENT_LIST_DIR}/skin_cook.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/cook*.cpp"
    )
    target_link_libraries(nwb_assets_mesh_cook PUBLIC nwb_assets nwb_assets_subsystem_cook nwb_assets_skeleton_cook nwb_mesh nwb_metascript nwb_assets_skeleton_types nwb_assets_mesh_types nwb_cpu_task)
    target_compile_definitions(nwb_assets_mesh_cook PRIVATE NWB_COOK=1)

    nwb_declare_static_library(nwb_assets_mesh_volume_entries)
    target_sources(nwb_assets_mesh_volume_entries PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/volume_entries.cpp"
    )
    target_link_libraries(nwb_assets_mesh_volume_entries PRIVATE nwb_assets_mesh_cook nwb_assets_subsystem_cook nwb_assets_cook)
    target_compile_definitions(nwb_assets_mesh_volume_entries PRIVATE NWB_COOK=1)
endif()
