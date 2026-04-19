nwb_declare_executable(nwb_math_tests)
target_sources(nwb_math_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/math_tests.cpp"
)
target_link_libraries(nwb_math_tests PRIVATE
    nwb_common
    nwb_alloc
)
add_test(NAME nwb_math_tests COMMAND nwb_math_tests)


nwb_declare_executable(nwb_math_scalar_tests)
target_sources(nwb_math_scalar_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/math_tests.cpp"
)
target_compile_definitions(nwb_math_scalar_tests PRIVATE NWB_MATH_FORCE_SCALAR=1)
target_link_libraries(nwb_math_scalar_tests PRIVATE
    nwb_common
    nwb_alloc
)
add_test(NAME nwb_math_scalar_tests COMMAND nwb_math_scalar_tests)


nwb_declare_executable(nwb_math_no_intrinsics_tests)
target_sources(nwb_math_no_intrinsics_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/math_tests.cpp"
    "${PROJECT_SOURCE_DIR}/global/detail/source_sh_math/source_sh_math.cpp"
    "${PROJECT_SOURCE_DIR}/global/detail/source_sh_math/source_sh_math.h"
)
target_compile_definitions(nwb_math_no_intrinsics_tests PRIVATE
    NWB_MATH_FORCE_SCALAR=1
    _MATH_NO_INTRINSICS_=1
)
target_link_libraries(nwb_math_no_intrinsics_tests PRIVATE
    nwb_common
    nwb_alloc
)
add_test(NAME nwb_math_no_intrinsics_tests COMMAND nwb_math_no_intrinsics_tests)


if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    nwb_declare_executable(nwb_math_sse_tests)
    target_sources(nwb_math_sse_tests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/math_tests.cpp"
    )
    if(NWB_COMPILER_FRONTEND_MSVC)
        target_compile_options(nwb_math_sse_tests PRIVATE
            /clang:-mno-avx2
            /clang:-mno-fma
            /clang:-mno-f16c
            /clang:-msse4.2
        )
    else()
        target_compile_options(nwb_math_sse_tests PRIVATE
            -mno-avx2
            -mno-fma
            -mno-f16c
            -msse4.2
        )
    endif()
    target_link_libraries(nwb_math_sse_tests PRIVATE
        nwb_common
        nwb_alloc
    )
    add_test(NAME nwb_math_sse_tests COMMAND nwb_math_sse_tests)
endif()
