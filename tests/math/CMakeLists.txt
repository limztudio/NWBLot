nwb_declare_executable(nwb_math_tests)
target_sources(nwb_math_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/math_tests.cpp"
)
target_link_libraries(nwb_math_tests PRIVATE
    nwb_common
    nwb_alloc
)
add_test(NAME nwb_math_tests COMMAND nwb_math_tests)
