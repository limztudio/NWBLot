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
