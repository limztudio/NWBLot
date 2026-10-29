nwb_declare_static_library(nwb_loader)
target_sources(nwb_loader PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/*.cc"
    "${CMAKE_CURRENT_LIST_DIR}/*.cxx"
    "${CMAKE_CURRENT_LIST_DIR}/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.h"
    "${CMAKE_CURRENT_LIST_DIR}/*.hh"
    "${CMAKE_CURRENT_LIST_DIR}/*.hxx"
    "${CMAKE_CURRENT_LIST_DIR}/*.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.inl"
    "${CMAKE_CURRENT_LIST_DIR}/*.ipp"
    "${CMAKE_CURRENT_LIST_DIR}/*.ixx"
)
nwb_target_sources_standalone_runtime(nwb_loader)
target_link_libraries(nwb_loader PUBLIC nwb_perf nwb_telemetry)
target_link_libraries(nwb_loader PRIVATE nwb_assets nwb_common nwb_crash nwb_alloc nwb_filesystem nwb_frame nwb_graphics nwb_shader_archive nwb_logclient nwb::cli11_headers)

if(TARGET nwb_crash_handler)
    add_dependencies(nwb_loader nwb_crash_handler)
endif()
