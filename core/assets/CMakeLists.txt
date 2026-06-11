nwb_declare_static_library(nwb_assets)
target_sources(nwb_assets PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/auto_registration.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/cooker.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/registry.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/manager.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/auto_registration.h"
    "${CMAKE_CURRENT_LIST_DIR}/global.h"
    "${CMAKE_CURRENT_LIST_DIR}/module.h"
    "${CMAKE_CURRENT_LIST_DIR}/binary_payload_io.h"
    "${CMAKE_CURRENT_LIST_DIR}/cooker.h"
    "${CMAKE_CURRENT_LIST_DIR}/paths.h"
    "${CMAKE_CURRENT_LIST_DIR}/ref.h"
    "${CMAKE_CURRENT_LIST_DIR}/registry.h"
    "${CMAKE_CURRENT_LIST_DIR}/manager.h"
)
target_link_libraries(nwb_assets PUBLIC nwb_common)

if(NWB_BUILD_RESOURCE_COOKER)
    nwb_declare_static_library(nwb_assets_cook)
    target_sources(nwb_assets_cook PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/cook_entry_registry.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/cook_entry_registry.h"
        "${CMAKE_CURRENT_LIST_DIR}/cook_paths.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/cook_paths.h"
        "${CMAKE_CURRENT_LIST_DIR}/cook_metadata.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/cook_metadata.h"
    )
    target_link_libraries(nwb_assets_cook PUBLIC nwb_assets nwb_metascript nwb_filesystem)
    target_compile_definitions(nwb_assets_cook PRIVATE NWB_COOK=1)
endif()
