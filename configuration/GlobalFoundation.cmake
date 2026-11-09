if(NWB_OBFUSCATE_STRING_LITERALS)
    add_library(nwb_string_literal_runtime OBJECT "${PROJECT_SOURCE_DIR}/global/string_literal_runtime.cpp")
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
    "${PROJECT_SOURCE_DIR}/global/arena_memory.cpp"
    "${PROJECT_SOURCE_DIR}/global/cpu_topology.cpp"
    "${PROJECT_SOURCE_DIR}/global/*.h"
    "${PROJECT_SOURCE_DIR}/global/*.hh"
    "${PROJECT_SOURCE_DIR}/global/*.hxx"
    "${PROJECT_SOURCE_DIR}/global/*.hpp"
    "${PROJECT_SOURCE_DIR}/global/*.inl"
    "${PROJECT_SOURCE_DIR}/global/*.ipp"
    "${PROJECT_SOURCE_DIR}/global/container/*.c"
    "${PROJECT_SOURCE_DIR}/global/container/*.cc"
    "${PROJECT_SOURCE_DIR}/global/container/*.cxx"
    "${PROJECT_SOURCE_DIR}/global/container/*.cpp"
    "${PROJECT_SOURCE_DIR}/global/container/*.h"
    "${PROJECT_SOURCE_DIR}/global/container/*.hh"
    "${PROJECT_SOURCE_DIR}/global/container/*.hxx"
    "${PROJECT_SOURCE_DIR}/global/container/*.hpp"
    "${PROJECT_SOURCE_DIR}/global/container/*.inl"
    "${PROJECT_SOURCE_DIR}/global/container/*.ipp"
    "${PROJECT_SOURCE_DIR}/global/container/*.ixx"
    "${PROJECT_SOURCE_DIR}/global/simd/*.c"
    "${PROJECT_SOURCE_DIR}/global/simd/*.cc"
    "${PROJECT_SOURCE_DIR}/global/simd/*.cxx"
    "${PROJECT_SOURCE_DIR}/global/simd/*.cpp"
    "${PROJECT_SOURCE_DIR}/global/simd/*.h"
    "${PROJECT_SOURCE_DIR}/global/simd/*.hh"
    "${PROJECT_SOURCE_DIR}/global/simd/*.hxx"
    "${PROJECT_SOURCE_DIR}/global/simd/*.hpp"
    "${PROJECT_SOURCE_DIR}/global/simd/*.inl"
    "${PROJECT_SOURCE_DIR}/global/simd/*.ipp"
    "${PROJECT_SOURCE_DIR}/global/simd/*.ixx"
    "${PROJECT_SOURCE_DIR}/global/unicode/*.c"
    "${PROJECT_SOURCE_DIR}/global/unicode/*.cc"
    "${PROJECT_SOURCE_DIR}/global/unicode/*.cxx"
    "${PROJECT_SOURCE_DIR}/global/unicode/*.cpp"
    "${PROJECT_SOURCE_DIR}/global/unicode/*.h"
    "${PROJECT_SOURCE_DIR}/global/unicode/*.hh"
    "${PROJECT_SOURCE_DIR}/global/unicode/*.hxx"
    "${PROJECT_SOURCE_DIR}/global/unicode/*.hpp"
    "${PROJECT_SOURCE_DIR}/global/unicode/*.inl"
    "${PROJECT_SOURCE_DIR}/global/unicode/*.ipp"
    "${PROJECT_SOURCE_DIR}/global/unicode/*.ixx"
)

