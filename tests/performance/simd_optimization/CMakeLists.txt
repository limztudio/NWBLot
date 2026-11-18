function(nwb_simd_benchmark_entry target benchmark_source benchmark_function argument_capacity)
    set(entry "${CMAKE_CURRENT_BINARY_DIR}/${target}_entry.cpp")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/console_entry.cpp.in" "${entry}" @ONLY)
    target_sources(${target} PRIVATE "${entry}")
    nwb_target_adhd_sources(${target} NAMESPACE "SimdOptimizationBenchmark" SOURCES "${CMAKE_CURRENT_LIST_DIR}/benchmark_cli.adhd")
    target_link_libraries(${target} PRIVATE nwb_tool_cli)
    if(NOT benchmark_source STREQUAL "memory_storage_probe")
        # Qualification samples the exception flags and rounding mode after individual operations.
        target_compile_options(${target} PRIVATE
            "$<$<CXX_COMPILER_ID:Clang,AppleClang>:-ffp-model=strict>"
            "$<$<CXX_COMPILER_ID:GNU>:-frounding-math;-ftrapping-math>"
            "$<$<CXX_COMPILER_ID:MSVC>:/fp:strict>"
        )
    endif()
endfunction()

nwb_declare_executable(nwb_simd_vector_benchmark)
nwb_target_adhd_sources(nwb_simd_vector_benchmark NAMESPACE "SimdOptimizationBenchmark" SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/vector_benchmark.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/binary_kernel_call.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/benchmark_support.adhd"
)
target_link_libraries(nwb_simd_vector_benchmark PRIVATE nwb_test_common_names nwb_common nwb_alloc)
nwb_simd_benchmark_entry(nwb_simd_vector_benchmark vector_benchmark RunVectorBenchmarkCli 8)
set_source_files_properties(
    "${CMAKE_BINARY_DIR}/adhd_gen/nwb_simd_vector_benchmark/tests/performance/simd_optimization/binary_kernel_call.adhd.cpp"
    PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON
)

nwb_declare_executable(nwb_simd_geometry_benchmark)
nwb_target_adhd_sources(nwb_simd_geometry_benchmark NAMESPACE "SimdOptimizationBenchmark" SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/geometry_benchmark.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/benchmark_support.adhd"
)
target_link_libraries(nwb_simd_geometry_benchmark PRIVATE nwb_test_common_names nwb_common nwb_alloc)
nwb_simd_benchmark_entry(nwb_simd_geometry_benchmark geometry_benchmark RunGeometryBenchmarkCli 4)

nwb_declare_executable(nwb_simd_memory_storage_benchmark)
nwb_target_adhd_sources(nwb_simd_memory_storage_benchmark NAMESPACE "SimdOptimizationBenchmark" SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/memory_probe_access.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/memory_probe_kernels.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/memory_measurement.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/memory_storage_probe.adhd"
)
target_link_libraries(nwb_simd_memory_storage_benchmark PRIVATE nwb_test_common_names nwb_common nwb_alloc)
nwb_simd_benchmark_entry(nwb_simd_memory_storage_benchmark memory_storage_probe RunMemoryStorageProbeCli 4)
set_source_files_properties(
    "${CMAKE_BINARY_DIR}/adhd_gen/nwb_simd_memory_storage_benchmark/tests/performance/simd_optimization/memory_storage_probe.adhd.cpp"
    PROPERTIES COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:Clang,AppleClang>:-ffp-model=strict>;$<$<CXX_COMPILER_ID:GNU>:-frounding-math>;$<$<CXX_COMPILER_ID:MSVC>:/fp:strict>"
)

nwb_declare_executable(nwb_simd_replicated_benchmark)
nwb_target_adhd_sources(nwb_simd_replicated_benchmark NAMESPACE "SimdOptimizationBenchmark" SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/replicated_benchmark.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/binary_kernel_call.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/benchmark_support.adhd"
)
target_link_libraries(nwb_simd_replicated_benchmark PRIVATE nwb_test_common_names nwb_common nwb_alloc)
nwb_simd_benchmark_entry(nwb_simd_replicated_benchmark replicated_benchmark RunReplicatedBenchmarkCli 8)
set_source_files_properties(
    "${CMAKE_BINARY_DIR}/adhd_gen/nwb_simd_replicated_benchmark/tests/performance/simd_optimization/binary_kernel_call.adhd.cpp"
    PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON
)

nwb_declare_executable(nwb_simd_mask_benchmark)
nwb_target_adhd_sources(nwb_simd_mask_benchmark NAMESPACE "SimdOptimizationBenchmark" SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/mask_benchmark.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/benchmark_support.adhd"
)
target_link_libraries(nwb_simd_mask_benchmark PRIVATE nwb_test_common_names nwb_common nwb_alloc)
nwb_simd_benchmark_entry(nwb_simd_mask_benchmark mask_benchmark RunMaskBenchmarkCli 8)

nwb_declare_executable(nwb_simd_box_normal_benchmark)
nwb_target_adhd_sources(nwb_simd_box_normal_benchmark NAMESPACE "SimdOptimizationBenchmark" SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/box_normal_benchmark.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/benchmark_support.adhd"
)
target_link_libraries(nwb_simd_box_normal_benchmark PRIVATE nwb_test_common_names nwb_common nwb_alloc)
nwb_simd_benchmark_entry(nwb_simd_box_normal_benchmark box_normal_benchmark RunBoxNormalBenchmarkCli 8)

nwb_declare_executable(nwb_simd_pow_qualification)
nwb_target_adhd_sources(nwb_simd_pow_qualification NAMESPACE "SimdOptimizationBenchmark" SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/pow_qualification.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/pow_packed_single.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/benchmark_support.adhd"
)
target_link_libraries(nwb_simd_pow_qualification PRIVATE nwb_test_common_names nwb_common nwb_alloc)
nwb_simd_benchmark_entry(nwb_simd_pow_qualification pow_qualification RunPowQualificationCli 4)


# The packed float candidate measures the default contraction and exception behavior.
set_source_files_properties(
    "${CMAKE_BINARY_DIR}/adhd_gen/nwb_simd_pow_qualification/tests/performance/simd_optimization/pow_packed_single.adhd.cpp"
    PROPERTIES COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:Clang,AppleClang>:-ffp-model=precise>;$<$<CXX_COMPILER_ID:GNU>:-fno-rounding-math>;$<$<CXX_COMPILER_ID:MSVC>:/fp:precise>"
)
