nwb_declare_static_library(nwb_assets_texture_types)
nwb_target_adhd_sources(nwb_assets_texture_types
    NAMESPACE NWB::Impl
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/types.adhd"
)
target_link_libraries(nwb_assets_texture_types PUBLIC nwb_assets)

nwb_declare_static_library(nwb_assets_texture)
target_sources(nwb_assets_texture PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/runtime.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/asset.h"
)
target_link_libraries(nwb_assets_texture PUBLIC nwb_assets nwb_assets_texture_types)

nwb_declare_static_library(nwb_assets_texture_loader)
target_sources(nwb_assets_texture_loader PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/loader.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/loader.h"
    "${CMAKE_CURRENT_LIST_DIR}/texture_mip_decoder.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/texture_mip_decoder.h"
)
target_link_libraries(nwb_assets_texture_loader PUBLIC nwb_assets_texture nwb_graphics)
target_link_libraries(nwb_assets_texture_loader PRIVATE nwb::basis_universal)

if(NWB_BUILD_PIPELINE)
    nwb_declare_static_library(nwb_assets_texture_cook)
    target_sources(nwb_assets_texture_cook PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/runtime.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/cook*.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/*.h"
        "${CMAKE_CURRENT_LIST_DIR}/*.hh"
        "${CMAKE_CURRENT_LIST_DIR}/*.hxx"
        "${CMAKE_CURRENT_LIST_DIR}/*.hpp"
        "${CMAKE_CURRENT_LIST_DIR}/*.inl"
        "${CMAKE_CURRENT_LIST_DIR}/*.ipp"
    )
    target_link_libraries(nwb_assets_texture_cook PUBLIC nwb_assets nwb_assets_texture_types nwb_metascript)
    target_compile_definitions(nwb_assets_texture_cook PRIVATE NWB_COOK=1)

    nwb_declare_static_library(nwb_assets_texture_volume_entry)
    target_sources(nwb_assets_texture_volume_entry PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/volume_entry.cpp"
    )
    target_link_libraries(nwb_assets_texture_volume_entry PRIVATE nwb_assets_texture_cook nwb_assets_cook)
    target_compile_definitions(nwb_assets_texture_volume_entry PRIVATE NWB_COOK=1)
endif()
