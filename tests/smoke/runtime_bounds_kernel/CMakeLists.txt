if(NOT TARGET nwb_assets_shader_cook)
    return()
endif()

# Execute the production current-pose bounds and optical union kernels through the native descriptor-buffer path.
nwb_declare_gtest_executable(nwb_runtime_bounds_kernel_tests)
target_sources(nwb_runtime_bounds_kernel_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/runtime_bounds_kernel_fixture.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/runtime_bounds_kernel_fixture.h"
    "${CMAKE_CURRENT_LIST_DIR}/local_bounds_kernel_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/optical_union_kernel_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../descriptor_buffer/round_trip/round_trip_fixture.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../descriptor_buffer/round_trip/round_trip_fixture.h"
)
target_compile_definitions(nwb_runtime_bounds_kernel_tests PRIVATE
    NWB_COOK=1
    NWB_RUNTIME_BOUNDS_KERNEL_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
    NWB_RUNTIME_BOUNDS_KERNEL_OUTPUT_ROOT="${CMAKE_CURRENT_BINARY_DIR}/kernels/$<CONFIG>"
)
target_link_libraries(nwb_runtime_bounds_kernel_tests PRIVATE
    nwb_frame
    nwb_graphics
    nwb_perf
    nwb_alloc
    nwb_common
    nwb_assets_graphics
    nwb_assets_shader_cook
    nwb_ecs_ui
)
set_tests_properties(nwb_runtime_bounds_kernel_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 300)
