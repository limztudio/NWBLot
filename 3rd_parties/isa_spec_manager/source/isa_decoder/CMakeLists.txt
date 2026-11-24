# Copyright (c) 2024 Advanced Micro Devices, Inc. All rights reserved.
cmake_minimum_required(VERSION 3.10)
include("${CMAKE_CURRENT_LIST_DIR}/../../../../configuration/UnityBuild.cmake")

set (SRC_DIR ../../source)
set (INC_DIR ../../include)

# Control the path of tinyxml2. NWB: tinyxml2 was flattened out of the bundled isa_spec_manager/source/third_party
# into top-level 3rd_parties/tinyxml2 (see .helper/3rd_parties.md).
set (TINYXML_DIR ${CMAKE_SOURCE_DIR}/3rd_parties/tinyxml2)
set (_NWB_USE_TINYXML2_TARGET OFF)
if (TARGET nwb::tinyxml2)
    set (_NWB_USE_TINYXML2_TARGET ON)
endif()
if (TINYXML_SRC_PATH)
    message("-- IsaDecoder: Overwritting default tinyxml2 source files with ${TINYXML_SRC_PATH}")
    set (TINYXML_DIR ${TINYXML_SRC_PATH})
    set (_NWB_USE_TINYXML2_TARGET OFF)
endif()

# Redirect to single build output location
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/output)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/output)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/output)

set (ALL_H
    ${INC_DIR}/amdisa/isa_decoder.h
    ${INC_DIR}/amdisa/api_version.h

    ${SRC_DIR}/common/amdisa_expression_tree_consts.h
    ${SRC_DIR}/common/amdisa_structures.h
    ${SRC_DIR}/common/amdisa_xml_element_consts.h
    ${SRC_DIR}/common/amdisa_utility.h
    ${SRC_DIR}/common/isa_xml_reader.h
    ${SRC_DIR}/common/xml_expression_tree_reader.h

    ./isa_decoder_strategy.h
    ./v1_1_1/encoding_condition_handler_v1_1_1.h

    ${TINYXML_DIR}/tinyxml2.h
)

set (ALL_CPP
    ./isa_decoder.cpp
    ./encoding_condition_handler.hpp
    ./v1_1_1/isa_decoder_v1_1_1.cpp

    ${SRC_DIR}/common/amdisa_utility.cpp
    ${SRC_DIR}/common/amdisa_structures.cpp
    ${SRC_DIR}/common/isa_xml_reader.cpp
    ${SRC_DIR}/common/xml_expression_tree_reader.cpp

)
if (NOT _NWB_USE_TINYXML2_TARGET)
    list(APPEND ALL_CPP ${TINYXML_DIR}/tinyxml2.cpp)
endif()

add_library(isa_decoder STATIC ${ALL_CPP})
target_include_directories(isa_decoder PRIVATE
    ${SRC_DIR}/common
    ${TINYXML_DIR}
    PUBLIC
    ${INC_DIR}
)
if (_NWB_USE_TINYXML2_TARGET)
    target_link_libraries(isa_decoder PRIVATE nwb::tinyxml2)
endif()
nwb_disable_vendor_warnings(isa_decoder)
nwb_skip_unity_build_sources(
    "${CMAKE_CURRENT_LIST_DIR}/v1_1_1/isa_decoder_v1_1_1.cpp"
)

install(DIRECTORY ${INC_DIR}/amdisa DESTINATION include COMPONENT dev)

install(TARGETS isa_decoder EXPORT isa_decoderTargets
        ARCHIVE DESTINATION lib COMPONENT binary
        LIBRARY DESTINATION lib COMPONENT binary
)

set_target_properties(isa_decoder PROPERTIES FOLDER "libs")
