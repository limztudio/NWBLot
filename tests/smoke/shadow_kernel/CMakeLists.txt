if(NOT TARGET nwb_assets_shader_cook OR NOT TARGET nwb_assets_csg_cook)
    return()
endif()

# Execute live shadow traversal and resolve shaders with analytic BVHs and synthetic textures in the native smoke fixture.
nwb_declare_gtest_executable(nwb_shadow_kernel_tests)
target_sources(nwb_shadow_kernel_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/shadow_kernel_fixture.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/shadow_kernel_fixture.h"
    "${CMAKE_CURRENT_LIST_DIR}/traversal_kernel_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/resolve_kernel_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/assets/traversal_cs.slang"
    "${CMAKE_CURRENT_LIST_DIR}/assets/traversal_cs.nwb"
    "${CMAKE_CURRENT_LIST_DIR}/assets/shadow/generated/transmittance_dispatch.slangi"
    "${CMAKE_CURRENT_LIST_DIR}/../descriptor_buffer/round_trip/round_trip_fixture.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../descriptor_buffer/round_trip/round_trip_fixture.h"
)
target_compile_definitions(nwb_shadow_kernel_tests PRIVATE
    NWB_COOK=1
    NWB_SHADOW_KERNEL_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
    NWB_SHADOW_KERNEL_OUTPUT_ROOT="${CMAKE_CURRENT_BINARY_DIR}/kernels/$<CONFIG>"
)
target_link_libraries(nwb_shadow_kernel_tests PRIVATE
    nwb_frame
    nwb_graphics
    nwb_perf
    nwb_alloc
    nwb_common
    nwb_assets_graphics
    nwb_assets_shader_cook
    nwb_assets_csg_cook
    nwb_ecs_ui
)
set_tests_properties(nwb_shadow_kernel_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 300)

set_source_files_properties(
    "${CMAKE_CURRENT_LIST_DIR}/assets/traversal_cs.slang"
    "${CMAKE_CURRENT_LIST_DIR}/assets/traversal_cs.nwb"
    "${CMAKE_CURRENT_LIST_DIR}/assets/shadow/generated/transmittance_dispatch.slangi"
    PROPERTIES HEADER_FILE_ONLY TRUE
)
