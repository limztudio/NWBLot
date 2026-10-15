nwb_declare_gtest_executable(nwb_math_tests)
target_sources(nwb_math_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/math_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/storage_equality_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/triangle_intersection_tests.cpp"
)
target_sources(nwb_math_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/double_constant_tests.cpp"
)
target_sources(nwb_math_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/native_reduction_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/native_mask_tests.cpp"
)
target_sources(nwb_math_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/box_normal_tests.cpp"
)
target_sources(nwb_math_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/vector_rounding_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/storage_boundary_tests.cpp"
)
target_link_libraries(nwb_math_tests PRIVATE nwb_common nwb_alloc)
target_sources(nwb_math_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/current_contract_boundary_tests.cpp"
)
target_sources(nwb_math_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/frustum_affine_boundary_tests.cpp"
)
target_sources(nwb_math_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/geometry_boundary_tests.cpp"
)
