nwb_declare_static_library(nwb_assets)
target_sources(nwb_assets PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/auto_registration.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/cooker.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/registry.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/manager.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/auto_registration.h"
    "${CMAKE_CURRENT_LIST_DIR}/global.h"
    "${CMAKE_CURRENT_LIST_DIR}/module.h"
    "${CMAKE_CURRENT_LIST_DIR}/cooker.h"
    "${CMAKE_CURRENT_LIST_DIR}/paths.h"
    "${CMAKE_CURRENT_LIST_DIR}/ref.h"
    "${CMAKE_CURRENT_LIST_DIR}/registry.h"
    "${CMAKE_CURRENT_LIST_DIR}/manager.h"
)
target_link_libraries(nwb_assets PUBLIC nwb_common)
