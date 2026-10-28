nwb_declare_gtest_executable(nwb_metascript_tests)
target_sources(nwb_metascript_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/metascript_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/reader_failure_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/adhd_compiler_test_common.h"
    "${CMAKE_CURRENT_LIST_DIR}/adhd_compiler_core_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/adhd_compiler_templates_interfaces_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/adhd_compiler_simd_parallel_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/adhd_compiler_specialization_containers_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/adhd_compiler_reflection_nwb_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/adhd_compiler_sema_validation_tests.cpp"
)
target_link_libraries(nwb_metascript_tests PRIVATE nwb_metascript nwb_common nwb_alloc)
