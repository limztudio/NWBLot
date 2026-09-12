if(NOT TARGET nwb_assets_shader_cook)
    return()
endif()

# Compile checked-in kernels with the production Slang cooker, then exercise real descriptor-buffer dispatch/readback.
nwb_declare_gtest_executable(nwb_reflection_kernel_tests)
target_sources(nwb_reflection_kernel_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/reflection_kernel_fixture.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/reflection_kernel_fixture.h"
    "${CMAKE_CURRENT_LIST_DIR}/depth_kernel_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/spatial_kernel_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/reference/spatial_cs.slang"
    "${CMAKE_CURRENT_LIST_DIR}/../descriptor_buffer/round_trip/round_trip_fixture.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../descriptor_buffer/round_trip/round_trip_fixture.h"
)
target_compile_definitions(nwb_reflection_kernel_tests PRIVATE
    NWB_COOK=1
    NWB_REFLECTION_KERNEL_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
    NWB_REFLECTION_KERNEL_OUTPUT_ROOT="${CMAKE_CURRENT_BINARY_DIR}/kernels/$<CONFIG>"
)
target_link_libraries(nwb_reflection_kernel_tests PRIVATE
    nwb_frame
    nwb_graphics
    nwb_perf
    nwb_alloc
    nwb_common
    nwb_assets_graphics
    nwb_assets_shader_cook
    nwb_ecs_ui
)
set_tests_properties(nwb_reflection_kernel_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 300)

set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/reference/spatial_cs.slang" PROPERTIES HEADER_FILE_ONLY TRUE)
