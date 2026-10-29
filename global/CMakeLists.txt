if(NWB_OBFUSCATE_STRING_LITERALS)
    add_library(nwb_string_literal_runtime OBJECT "${CMAKE_CURRENT_LIST_DIR}/string_literal_runtime.cpp")
    set_target_properties(nwb_string_literal_runtime PROPERTIES CXX_COMPILER_LAUNCHER "" POSITION_INDEPENDENT_CODE ON)
    target_include_directories(nwb_string_literal_runtime PRIVATE "${PROJECT_SOURCE_DIR}")
    nwb_apply_codegen(nwb_string_literal_runtime)
    # Only scalar aliases are used; header-injected CRT/iterator directives must not constrain vendor images.
    if(NWB_COMPILER_FRONTEND_MSVC)
        target_compile_options(nwb_string_literal_runtime PRIVATE /clang:-fno-autolink)
    else()
        target_compile_options(nwb_string_literal_runtime PRIVATE -fno-autolink)
    endif()
endif()

nwb_declare_static_library(global_foundation)
target_sources(global_foundation PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/arena_memory.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/cpu_topology.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.h"
    "${CMAKE_CURRENT_LIST_DIR}/*.hh"
    "${CMAKE_CURRENT_LIST_DIR}/*.hxx"
    "${CMAKE_CURRENT_LIST_DIR}/*.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.inl"
    "${CMAKE_CURRENT_LIST_DIR}/*.ipp"
    "${CMAKE_CURRENT_LIST_DIR}/container/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/container/*.cc"
    "${CMAKE_CURRENT_LIST_DIR}/container/*.cxx"
    "${CMAKE_CURRENT_LIST_DIR}/container/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/container/*.h"
    "${CMAKE_CURRENT_LIST_DIR}/container/*.hh"
    "${CMAKE_CURRENT_LIST_DIR}/container/*.hxx"
    "${CMAKE_CURRENT_LIST_DIR}/container/*.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/container/*.inl"
    "${CMAKE_CURRENT_LIST_DIR}/container/*.ipp"
    "${CMAKE_CURRENT_LIST_DIR}/container/*.ixx"
    "${CMAKE_CURRENT_LIST_DIR}/simd/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/simd/*.cc"
    "${CMAKE_CURRENT_LIST_DIR}/simd/*.cxx"
    "${CMAKE_CURRENT_LIST_DIR}/simd/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/simd/*.h"
    "${CMAKE_CURRENT_LIST_DIR}/simd/*.hh"
    "${CMAKE_CURRENT_LIST_DIR}/simd/*.hxx"
    "${CMAKE_CURRENT_LIST_DIR}/simd/*.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/simd/*.inl"
    "${CMAKE_CURRENT_LIST_DIR}/simd/*.ipp"
    "${CMAKE_CURRENT_LIST_DIR}/simd/*.ixx"
    "${CMAKE_CURRENT_LIST_DIR}/unicode/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/unicode/*.cc"
    "${CMAKE_CURRENT_LIST_DIR}/unicode/*.cxx"
    "${CMAKE_CURRENT_LIST_DIR}/unicode/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/unicode/*.h"
    "${CMAKE_CURRENT_LIST_DIR}/unicode/*.hh"
    "${CMAKE_CURRENT_LIST_DIR}/unicode/*.hxx"
    "${CMAKE_CURRENT_LIST_DIR}/unicode/*.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/unicode/*.inl"
    "${CMAKE_CURRENT_LIST_DIR}/unicode/*.ipp"
    "${CMAKE_CURRENT_LIST_DIR}/unicode/*.ixx"
)
