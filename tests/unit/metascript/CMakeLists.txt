nwb_declare_gtest_executable(nwb_metascript_tests)
nwb_target_adhd_sources(nwb_metascript_tests NAMESPACE "NWB::Tests::Metascript" SOURCES "${CMAKE_CURRENT_LIST_DIR}/suite.adhd" "${CMAKE_CURRENT_LIST_DIR}/metascript_simd_and_specialization_cases.adhd" "${CMAKE_CURRENT_LIST_DIR}/metascript_runtime_and_reflection_cases.adhd")
target_link_libraries(nwb_metascript_tests PRIVATE nwb_metascript nwb_common nwb_alloc)

if(TARGET nwb_adhd_compiler)
    nwb_target_adhd_sources(nwb_metascript_tests NAMESPACE "NWB::Tests" SOURCES "${CMAKE_CURRENT_LIST_DIR}/generated/reflection_container.adhd" "${CMAKE_CURRENT_LIST_DIR}/generated/compact_string.adhd" "${CMAKE_CURRENT_LIST_DIR}/generated/builtin_shadow_global.adhd")
    target_compile_definitions(nwb_metascript_tests PRIVATE NWB_METASCRIPT_GENERATED_TESTS=1)
endif()
