nwb_declare_static_library(nwb_assets)
target_sources(nwb_assets PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/auto_registration.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/binary_payload_io.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/metadata_fields.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/module.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/paths.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/registry.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/manager.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/gather_merge_registry.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.h"
    "${CMAKE_CURRENT_LIST_DIR}/*.hh"
    "${CMAKE_CURRENT_LIST_DIR}/*.hxx"
    "${CMAKE_CURRENT_LIST_DIR}/*.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.inl"
    "${CMAKE_CURRENT_LIST_DIR}/*.ipp"
)
target_link_libraries(nwb_assets PUBLIC nwb_common)

if(NWB_BUILD_PIPELINE)
    nwb_declare_static_library(nwb_assets_cook)
    target_sources(nwb_assets_cook PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/input_list.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/module.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/cook_dependency_registry.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/cook_entry_registry.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/cook_metadata.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/cook_metadata_cache.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/cook_metadata_registry.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/cook_paths.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/*.h"
        "${CMAKE_CURRENT_LIST_DIR}/*.hh"
        "${CMAKE_CURRENT_LIST_DIR}/*.hxx"
        "${CMAKE_CURRENT_LIST_DIR}/*.hpp"
        "${CMAKE_CURRENT_LIST_DIR}/*.inl"
        "${CMAKE_CURRENT_LIST_DIR}/*.ipp"
    )
    target_link_libraries(nwb_assets_cook PUBLIC nwb_assets nwb_metascript nwb_filesystem nwb_cpu_task)
    target_compile_definitions(nwb_assets_cook PRIVATE NWB_COOK=1)
endif()

add_subdirectory(bunch)
add_subdirectory(volume)

