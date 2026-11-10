file(GLOB _nwb_loader_adhd_sources CONFIGURE_DEPENDS LIST_DIRECTORIES false "${CMAKE_CURRENT_LIST_DIR}/*.adhd")
list(REMOVE_ITEM _nwb_loader_adhd_sources
    "${CMAKE_CURRENT_LIST_DIR}/callback_shutdown.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/project_entry.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/runtime_constants.adhd"
)

nwb_declare_static_library(nwb_loader_runtime_constants)
nwb_target_adhd_sources(nwb_loader_runtime_constants NAMESPACE "NWB::LoaderDetail" SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/callback_shutdown.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/runtime_constants.adhd"
)
nwb_target_adhd_sources(nwb_loader_runtime_constants NAMESPACE "NWB" SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/project_entry.adhd"
)

nwb_declare_static_library(nwb_loader)
nwb_target_adhd_sources(nwb_loader NAMESPACE "NWB::LoaderDetail" SOURCES ${_nwb_loader_adhd_sources})
unset(_nwb_loader_adhd_sources)
nwb_target_sources_standalone_runtime(nwb_loader)
target_link_libraries(nwb_loader PUBLIC nwb_perf nwb_telemetry nwb_loader_runtime_constants)
target_link_libraries(nwb_loader PRIVATE nwb_assets nwb_common nwb_crash nwb_alloc nwb_filesystem nwb_frame nwb_graphics nwb_shader_archive nwb_logclient nwb::cli11_headers)

if(TARGET nwb_crash_handler)
    add_dependencies(nwb_loader nwb_crash_handler)
endif()
