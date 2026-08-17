add_test(
    NAME nwb_return_value_handling
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/return_value_handling.py"
        "${CMAKE_SOURCE_DIR}"
)
add_test(
    NAME nwb_return_value_handling_self_test
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/return_value_handling.py"
        --self-test
)
add_test(
    NAME nwb_no_direct_std_in_tests
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/no_direct_std_in_tests.py"
        "${CMAKE_SOURCE_DIR}"
)
add_test(
    NAME nwb_no_direct_std_in_tests_self_test
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/no_direct_std_in_tests.py"
        --self-test
)
add_test(
    NAME nwb_no_direct_std_in_interop_boundaries
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/no_direct_std_in_interop_boundaries.py"
        "${CMAKE_SOURCE_DIR}"
)
add_test(
    NAME nwb_no_direct_std_in_interop_boundaries_self_test
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/no_direct_std_in_interop_boundaries.py"
        --self-test
)
add_test(
    NAME nwb_no_core_implementation_dependencies
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/no_core_implementation_dependencies.py"
        "${CMAKE_SOURCE_DIR}"
)
add_test(
    NAME nwb_no_core_implementation_dependencies_self_test
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/no_core_implementation_dependencies.py"
        --self-test
)
add_test(
    NAME nwb_no_engine_default_bxdf
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/no_engine_default_bxdf.py"
        "${CMAKE_SOURCE_DIR}"
)
add_test(
    NAME nwb_no_engine_default_bxdf_self_test
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/no_engine_default_bxdf.py"
        --self-test
)
add_test(
    NAME nwb_no_hidden_namespaces_in_headers
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/no_hidden_namespaces_in_headers.py"
        "${CMAKE_SOURCE_DIR}"
)
add_test(
    NAME nwb_no_hidden_namespaces_in_headers_self_test
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/no_hidden_namespaces_in_headers.py"
        --self-test
)
add_test(
    NAME nwb_test_feature_override_boundary
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/test_feature_override_boundary.py"
        "${CMAKE_SOURCE_DIR}"
)
add_test(
    NAME nwb_test_feature_override_boundary_self_test
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/test_feature_override_boundary.py"
        --self-test
)
