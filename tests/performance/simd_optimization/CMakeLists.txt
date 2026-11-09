nwb_declare_executable(nwb_simd_vector_benchmark)
nwb_target_adhd_sources(nwb_simd_vector_benchmark NAMESPACE "SimdOptimizationBenchmark" SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/vector_benchmark.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/benchmark_support.adhd"
)
target_link_libraries(nwb_simd_vector_benchmark PRIVATE nwb_common nwb_alloc)

nwb_declare_executable(nwb_simd_geometry_benchmark)
nwb_target_adhd_sources(nwb_simd_geometry_benchmark NAMESPACE "SimdOptimizationBenchmark" SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/geometry_benchmark.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/benchmark_support.adhd"
)
target_link_libraries(nwb_simd_geometry_benchmark PRIVATE nwb_common nwb_alloc)

nwb_declare_executable(nwb_simd_memory_storage_benchmark)
nwb_target_adhd_sources(nwb_simd_memory_storage_benchmark NAMESPACE "SimdOptimizationBenchmark" SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/memory_storage_probe.adhd"
)
target_link_libraries(nwb_simd_memory_storage_benchmark PRIVATE nwb_common nwb_alloc)

nwb_declare_executable(nwb_simd_replicated_benchmark)
nwb_target_adhd_sources(nwb_simd_replicated_benchmark NAMESPACE "SimdOptimizationBenchmark" SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/replicated_benchmark.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/benchmark_support.adhd"
)
target_link_libraries(nwb_simd_replicated_benchmark PRIVATE nwb_common nwb_alloc)

nwb_declare_executable(nwb_simd_mask_benchmark)
nwb_target_adhd_sources(nwb_simd_mask_benchmark NAMESPACE "SimdOptimizationBenchmark" SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/mask_benchmark.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/benchmark_support.adhd"
)
target_link_libraries(nwb_simd_mask_benchmark PRIVATE nwb_common nwb_alloc)

nwb_declare_executable(nwb_simd_box_normal_benchmark)
nwb_target_adhd_sources(nwb_simd_box_normal_benchmark NAMESPACE "SimdOptimizationBenchmark" SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/box_normal_benchmark.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/benchmark_support.adhd"
)
target_link_libraries(nwb_simd_box_normal_benchmark PRIVATE nwb_common nwb_alloc)

nwb_declare_executable(nwb_simd_pow_qualification)
nwb_target_adhd_sources(nwb_simd_pow_qualification NAMESPACE "SimdOptimizationBenchmark" SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/pow_qualification.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/benchmark_support.adhd"
)
target_link_libraries(nwb_simd_pow_qualification PRIVATE nwb_common nwb_alloc)
