# NWB-rewritten RmvParser CMakeLists for the offline/vendored RGD build. The RMT (Radeon Memory Trace) parser
# backs RGD's page-fault resource-history section. Differences from upstream: built as a plain static lib
# without devtools_target_options (its -Werror would turn vendored warnings into errors under clang), include
# paths target the flattened 3rd_parties layout (rdf/inc/amdrdf.h), and the CRT / iterator-debug level is
# matched to the engine by the parent via nwb_match_engine_runtime.
cmake_minimum_required(VERSION 3.10)
project(RmvParser)

set(_3P "${CMAKE_SOURCE_DIR}/3rd_parties")

# CMAKE_INCLUDE_CURRENT_DIR-style same-dir includes ("rmt_*.h") resolve via PROJECT_SOURCE_DIR below.
file(GLOB RMV_PARSER_SRC "${PROJECT_SOURCE_DIR}/*.cpp")
if(NOT WIN32)
    list(APPEND RMV_PARSER_SRC "${PROJECT_SOURCE_DIR}/linux/safe_crt.cpp")
endif()

add_library(RmvParser STATIC ${RMV_PARSER_SRC})

target_include_directories(RmvParser
    PUBLIC "${PROJECT_SOURCE_DIR}"
    PRIVATE "${_3P}")                 # rdf/inc/amdrdf.h

# amdrdf carries RDF_CXX_BINDINGS=1 PUBLIC, so the rdf C++ bindings the RMT parser uses come transitively.
target_link_libraries(RmvParser PUBLIC amdrdf)

set_target_properties(RmvParser PROPERTIES
    CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON)
nwb_disable_vendor_warnings(RmvParser)
