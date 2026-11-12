file(GLOB _nwb_logserver_core_adhd CONFIGURE_DEPENDS LIST_DIRECTORIES false "${CMAKE_CURRENT_LIST_DIR}/*.adhd")
list(REMOVE_ITEM _nwb_logserver_core_adhd "${CMAKE_CURRENT_LIST_DIR}/main.adhd")

nwb_declare_static_library(nwb_logserver_core)
nwb_target_adhd_sources(nwb_logserver_core NAMESPACE "NWB::Log" SOURCES ${_nwb_logserver_core_adhd})
unset(_nwb_logserver_core_adhd)
if(WIN32)
    target_link_libraries(nwb_logserver_core PRIVATE dbghelp)
endif()
# Radeon GPU Detective in-process .rgd decoder.
# The logger-owned adapter consumes the vendored amdrdf/system_info/isa_decoder/comgr_utils/pix stack.
target_link_libraries(nwb_logserver_core
    PUBLIC
        nwb::microhttpd
        nwb_common
        nwb_alloc
        nwb_crash
        nwb_logtelemetry
        nwb_logrgd
    PRIVATE
        nwb::aftermath_headers
)

nwb_declare_executable(nwb_logserver)
nwb_target_adhd_sources(nwb_logserver NAMESPACE "NWB::Log" SOURCES "${CMAKE_CURRENT_LIST_DIR}/main.adhd")
nwb_target_sources_standalone_runtime(nwb_logserver)
if(WIN32)
    set_target_properties(nwb_logserver PROPERTIES WIN32_EXECUTABLE ON)
endif()
target_link_libraries(nwb_logserver PRIVATE nwb_logserver_core nwb::cli11_headers)
nwb_copy_aftermath_runtime(nwb_logserver)
