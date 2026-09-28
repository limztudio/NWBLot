set(_nwb_font_runtime_sources
    "${CMAKE_CURRENT_LIST_DIR}/runtime.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/font_validation.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/asset.h"
    "${CMAKE_CURRENT_LIST_DIR}/binary_payload.h"
    "${CMAKE_CURRENT_LIST_DIR}/font_validation.h"
)

nwb_declare_static_library(nwb_assets_font)
target_sources(nwb_assets_font PRIVATE ${_nwb_font_runtime_sources})
target_link_libraries(nwb_assets_font PUBLIC nwb_assets PRIVATE nwb::freetype)

if(NWB_BUILD_PIPELINE)
    nwb_declare_static_library(nwb_assets_font_cook)
    target_sources(nwb_assets_font_cook PRIVATE
        ${_nwb_font_runtime_sources}
        "${CMAKE_CURRENT_LIST_DIR}/cook.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/cook_metadata.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/cook.h"
    )
    target_link_libraries(nwb_assets_font_cook PUBLIC nwb_assets nwb_metascript PRIVATE nwb::freetype)
    target_compile_definitions(nwb_assets_font_cook PRIVATE NWB_COOK=1)

    nwb_declare_static_library(nwb_assets_font_volume_entry)
    target_sources(nwb_assets_font_volume_entry PRIVATE "${CMAKE_CURRENT_LIST_DIR}/volume_entry.cpp")
    target_link_libraries(nwb_assets_font_volume_entry PRIVATE nwb_assets_font_cook nwb_assets_cook)
    target_compile_definitions(nwb_assets_font_volume_entry PRIVATE NWB_COOK=1)
endif()
