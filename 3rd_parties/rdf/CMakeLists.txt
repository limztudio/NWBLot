# Build rdf statically with zstd for the offline RGD build.
# RDF_BUILD_STATIC=1 keeps RDF_EXPORT empty for static linking.
cmake_minimum_required(VERSION 3.13)
project(rdf LANGUAGES CXX C)
include("${CMAKE_CURRENT_LIST_DIR}/../../configuration/UnityBuild.cmake")

add_library(amdrdf STATIC
    "${CMAKE_CURRENT_LIST_DIR}/src/amdrdf.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/inc/amdrdf.h")
add_library(rdf ALIAS amdrdf)

target_include_directories(amdrdf PUBLIC "${CMAKE_CURRENT_LIST_DIR}/inc")
target_link_libraries(amdrdf PRIVATE zstd)

target_compile_definitions(amdrdf PRIVATE RDF_BUILD_LIBRARY RDF_BUILD_STATIC=1)
# RDF_CXX_BINDINGS=1 PUBLIC exposes the rdf::Stream / rdf::ChunkFile C++ wrappers the RGD + RMT parsers use.
target_compile_definitions(amdrdf PUBLIC RDF_CXX_BINDINGS=1)
if(WIN32)
    target_compile_definitions(amdrdf PRIVATE RDF_PLATFORM_WINDOWS=1 _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_definitions(amdrdf PRIVATE RDF_PLATFORM_UNIX=1)
endif()

set_target_properties(amdrdf PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON)
nwb_disable_vendor_warnings(amdrdf)
