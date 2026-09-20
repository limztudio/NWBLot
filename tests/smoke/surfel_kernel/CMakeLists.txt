if(NOT TARGET nwb_assets_shader_cook OR NOT TARGET nwb_assets_csg_cook)
    return()
endif()

nwb_declare_gtest_executable(nwb_surfel_kernel_tests)
target_sources(nwb_surfel_kernel_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/gather_field_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../descriptor_buffer/round_trip/round_trip_fixture.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../descriptor_buffer/round_trip/round_trip_fixture.h"
)
set(surfel_test_assets
    "${CMAKE_CURRENT_LIST_DIR}/assets/gather_field_cs.slang"
    "${CMAKE_CURRENT_LIST_DIR}/assets/gather_field_cs.nwb"
    "${CMAKE_CURRENT_LIST_DIR}/reference/surfel_gather_field.slangi"
)
target_sources(nwb_surfel_kernel_tests PRIVATE ${surfel_test_assets})
set_source_files_properties(${surfel_test_assets} PROPERTIES HEADER_FILE_ONLY TRUE)
target_compile_definitions(nwb_surfel_kernel_tests PRIVATE
    NWB_COOK=1
    NWB_SURFEL_KERNEL_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
    NWB_SURFEL_KERNEL_OUTPUT_ROOT="${CMAKE_CURRENT_BINARY_DIR}/kernels/$<CONFIG>"
)
target_link_libraries(nwb_surfel_kernel_tests PRIVATE
    nwb_frame nwb_graphics nwb_perf nwb_alloc nwb_common
    nwb_assets_graphics nwb_assets_shader_cook nwb_assets_csg_cook nwb_ecs_ui
)
set_tests_properties(nwb_surfel_kernel_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 300)
