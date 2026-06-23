# NWB-rewritten RGD backend CMakeLists for an offline/vendored, in-process .rgd decoder build.
# Differences from upstream: no FetchContent (the comgr lib + ISA-spec XML are optional runtime-deploy data,
# like Aftermath's DLL); link the engine's vendored miniz instead of compiling a bundled copy; exclude the
# DXBC (DirectX bytecode) parser, which needs DXC and is irrelevant to a Vulkan engine. amd_gpu_dis / amd_comgr
# are dlopen'd at runtime, so the disassembly degrades gracefully if those runtime libs / the ISA XML are absent.
cmake_minimum_required(VERSION 3.10)
project(rgd_backend)

set(_RGD_3P "${CMAKE_SOURCE_DIR}/3rd_parties")

# Build the crash-SUMMARY decoder (markers, page fault, registers, resources, system info). The shader
# disassembly / code-object files are excluded: they need amd_gpu_dis's CodeObjectDisassemblerApi.h, which
# ships only inside Radeon GPU Analyzer (not in RGD's dependency map). rgd_dxbc_parser is DirectX-only.
# Drop in the amd_gpu_dis package + re-include these to enable live ISA disassembly later.
file(GLOB RGD_BACKEND_SRC "${PROJECT_SOURCE_DIR}/*.cpp")
list(FILTER RGD_BACKEND_SRC EXCLUDE REGEX
    "rgd_dxbc_parser|rgd_process_utils|rgd_amd_gpu_dis_loader|rgd_code_object_database|rgd_srd_disassembler|rgd_srd_instruction_analyzer|rgd_enhanced_crash_info_serializer")

add_library(rgd_backend STATIC ${RGD_BACKEND_SRC})
add_library(nwb::rgd_backend ALIAS rgd_backend)

target_include_directories(rgd_backend PUBLIC "${PROJECT_SOURCE_DIR}")
target_include_directories(rgd_backend PRIVATE
    "${_RGD_3P}"                              # rmv/source/..., dev_driver/include/... (root-relative includes)
    "${_RGD_3P}/dev_driver/include"           # rgdevents.h
    "${_RGD_3P}/rmv/source/parser"
    "${_RGD_3P}/rmv/source/backend"
    "${_RGD_3P}/isa_spec_manager/include")

# nwb::nlohmann_json provides <nlohmann/json.hpp> (the single shared vendored copy); rgd_data_types.h pulls it
# into the PUBLIC headers, so the link is PUBLIC.
target_link_libraries(rgd_backend
    PUBLIC amdrdf system_info AMD::ComgrUtils nwb::miniz nwb::nlohmann_json nwb::dev_driver_headers
    PRIVATE isa_decoder RmvBackend)
if(WIN32)
    target_link_libraries(rgd_backend PRIVATE pix_events_decoder)
endif()

# SYSTEM_INFO_ENABLE_RDF (selects system_info's rdf::ChunkFile Parse overload) and RDF_CXX_BINDINGS (the
# rdf::ChunkFile C++ wrapper) both arrive transitively: the former PUBLIC from system_info once
# SYSTEM_INFO_BUILD_RDF_INTERFACES is on, the latter PUBLIC from amdrdf.
target_compile_definitions(rgd_backend PRIVATE NOMINMAX)
set_target_properties(rgd_backend PROPERTIES
    CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON)
nwb_disable_vendor_warnings(rgd_backend)
