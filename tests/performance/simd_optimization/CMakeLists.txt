nwb_declare_executable(nwb_simd_vector_benchmark)
target_sources(nwb_simd_vector_benchmark PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/vector_benchmark.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/benchmark_support.h"
)
target_link_libraries(nwb_simd_vector_benchmark PRIVATE nwb_common nwb_alloc)

nwb_declare_executable(nwb_simd_geometry_benchmark)
target_sources(nwb_simd_geometry_benchmark PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/geometry_benchmark.cpp"
)
target_link_libraries(nwb_simd_geometry_benchmark PRIVATE nwb_common nwb_alloc)

nwb_declare_executable(nwb_simd_memory_storage_benchmark)
target_sources(nwb_simd_memory_storage_benchmark PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/memory_storage_probe.cpp"
)
target_link_libraries(nwb_simd_memory_storage_benchmark PRIVATE nwb_common nwb_alloc)

nwb_declare_executable(nwb_simd_replicated_benchmark)
target_sources(nwb_simd_replicated_benchmark PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/replicated_benchmark.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/benchmark_support.h"
)
target_link_libraries(nwb_simd_replicated_benchmark PRIVATE nwb_common nwb_alloc)

nwb_declare_executable(nwb_simd_mask_benchmark)
target_sources(nwb_simd_mask_benchmark PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/mask_benchmark.cpp"
)
target_link_libraries(nwb_simd_mask_benchmark PRIVATE nwb_common nwb_alloc)

nwb_declare_executable(nwb_simd_box_normal_benchmark)
target_sources(nwb_simd_box_normal_benchmark PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/box_normal_benchmark.cpp"
)
target_link_libraries(nwb_simd_box_normal_benchmark PRIVATE nwb_common nwb_alloc)
