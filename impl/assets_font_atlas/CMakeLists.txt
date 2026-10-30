nwb_declare_static_library(nwb_assets_font_atlas_types)
nwb_target_adhd_sources(nwb_assets_font_atlas_types
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/*.adhd"
)
target_link_libraries(nwb_assets_font_atlas_types PUBLIC nwb_assets)

set(_nwb_font_atlas_runtime_sources
    "${CMAKE_CURRENT_LIST_DIR}/runtime.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/binary.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/validation.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/source_*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/positioning_validation.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.h"
    "${CMAKE_CURRENT_LIST_DIR}/*.hh"
    "${CMAKE_CURRENT_LIST_DIR}/*.hxx"
    "${CMAKE_CURRENT_LIST_DIR}/*.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.inl"
    "${CMAKE_CURRENT_LIST_DIR}/*.ipp"
)

nwb_declare_static_library(nwb_assets_font_atlas)
target_sources(nwb_assets_font_atlas PRIVATE
    ${_nwb_font_atlas_runtime_sources}
)
target_link_libraries(nwb_assets_font_atlas PUBLIC nwb_assets_font nwb_assets_font_atlas_types)

if(NWB_BUILD_PIPELINE)
    nwb_declare_static_library(nwb_assets_font_atlas_cook)
    target_sources(nwb_assets_font_atlas_cook PRIVATE
        ${_nwb_font_atlas_runtime_sources}
        "${CMAKE_CURRENT_LIST_DIR}/cook*.cpp"
    )
    target_link_libraries(nwb_assets_font_atlas_cook PUBLIC nwb_assets_font_cook nwb_assets_font_atlas_types nwb_metascript)
    target_compile_definitions(nwb_assets_font_atlas_cook PRIVATE NWB_COOK=1)

    nwb_declare_static_library(nwb_assets_font_atlas_volume_entry)
    target_sources(nwb_assets_font_atlas_volume_entry PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/volume_entry.cpp"
    )
    target_link_libraries(nwb_assets_font_atlas_volume_entry PRIVATE nwb_assets_font_atlas_cook nwb_assets_font_cook nwb_assets_cook)
    target_compile_definitions(nwb_assets_font_atlas_volume_entry PRIVATE NWB_COOK=1)
endif()
