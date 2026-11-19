nwb_declare_static_library(nwb_logrgd)
nwb_target_adhd_sources(nwb_logrgd NAMESPACE "NWB::Log" SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/arena_names.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/decode.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/decode_values.adhd"
)
target_sources(nwb_logrgd PRIVATE "${CMAKE_CURRENT_LIST_DIR}/decode.cpp")
target_include_directories(nwb_logrgd SYSTEM PRIVATE "${PROJECT_SOURCE_DIR}/3rd_parties")
target_link_libraries(nwb_logrgd PUBLIC nwb_common nwb_alloc PRIVATE nwb::rgd_backend)
