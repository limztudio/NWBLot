nwb_declare_static_library(nwb_filesystem)
target_sources(nwb_filesystem PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/volume_build.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/volume_file_system.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/volume_io.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/volume_metadata.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/volume_session.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/volume_staging.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/global.h"
    "${CMAKE_CURRENT_LIST_DIR}/module.h"
    "${CMAKE_CURRENT_LIST_DIR}/arena_names.h"
    "${CMAKE_CURRENT_LIST_DIR}/volume_build.h"
    "${CMAKE_CURRENT_LIST_DIR}/volume_file_system.h"
    "${CMAKE_CURRENT_LIST_DIR}/volume_session.h"
    "${CMAKE_CURRENT_LIST_DIR}/volume_staging.h"
    "${CMAKE_CURRENT_LIST_DIR}/volume_types.h"
    "${CMAKE_CURRENT_LIST_DIR}/volume_staging_detail.h"
    "${CMAKE_CURRENT_LIST_DIR}/volume_storage_detail.h"
)
target_link_libraries(nwb_filesystem PUBLIC nwb_common nwb_alloc)
