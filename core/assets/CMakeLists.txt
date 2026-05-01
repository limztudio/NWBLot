nwb_declare_static_library(nwb_assets)
target_sources(nwb_assets PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/asset_auto_registration.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/asset_cooker.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/asset_registry.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/asset_manager.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/asset_auto_registration.h"
    "${CMAKE_CURRENT_LIST_DIR}/global.h"
    "${CMAKE_CURRENT_LIST_DIR}/asset.h"
    "${CMAKE_CURRENT_LIST_DIR}/asset_cooker.h"
    "${CMAKE_CURRENT_LIST_DIR}/asset_paths.h"
    "${CMAKE_CURRENT_LIST_DIR}/asset_ref.h"
    "${CMAKE_CURRENT_LIST_DIR}/asset_registry.h"
    "${CMAKE_CURRENT_LIST_DIR}/asset_manager.h"
)
target_link_libraries(nwb_assets PUBLIC nwb_common)
