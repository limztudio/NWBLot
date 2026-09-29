set(_nwb_font_atlas_runtime_sources
    "${CMAKE_CURRENT_LIST_DIR}/runtime.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/binary.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/validation.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/source_match.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/positioning_validation.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/asset.h"
    "${CMAKE_CURRENT_LIST_DIR}/model.h"
    "${CMAKE_CURRENT_LIST_DIR}/binary_payload.h"
)

nwb_declare_static_library(nwb_assets_font_atlas)
target_sources(nwb_assets_font_atlas PRIVATE ${_nwb_font_atlas_runtime_sources})
target_link_libraries(nwb_assets_font_atlas PUBLIC nwb_assets_font)

nwb_declare_static_library(nwb_assets_font_atlas_source)
target_sources(nwb_assets_font_atlas_source PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/source_payload.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/source_payload.h"
)
target_link_libraries(nwb_assets_font_atlas_source PUBLIC nwb_assets_font_atlas PRIVATE zstd)

if(NWB_BUILD_PIPELINE)
    nwb_declare_static_library(nwb_assets_font_atlas_cook)
    target_sources(nwb_assets_font_atlas_cook PRIVATE
        ${_nwb_font_atlas_runtime_sources}
        "${CMAKE_CURRENT_LIST_DIR}/cook.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/cook_metadata.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/cook_metadata_records.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/cook_metadata_values.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/cook_metadata_payload.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/cook.h"
        "${CMAKE_CURRENT_LIST_DIR}/cook_metadata.h"
    )
    target_link_libraries(nwb_assets_font_atlas_cook PUBLIC nwb_assets_font nwb_metascript nwb_assets_font_atlas_source)
    target_compile_definitions(nwb_assets_font_atlas_cook PRIVATE NWB_COOK=1)

    nwb_declare_static_library(nwb_assets_font_atlas_volume_entry)
    target_sources(nwb_assets_font_atlas_volume_entry PRIVATE "${CMAKE_CURRENT_LIST_DIR}/volume_entry.cpp")
    target_link_libraries(nwb_assets_font_atlas_volume_entry PRIVATE nwb_assets_font_atlas_cook nwb_assets_cook)
    target_compile_definitions(nwb_assets_font_atlas_volume_entry PRIVATE NWB_COOK=1)
endif()
