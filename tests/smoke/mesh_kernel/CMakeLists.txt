if(NOT TARGET nwb_assets_shader_cook OR NOT TARGET nwb_assets_csg_cook)
    return()
endif()

nwb_declare_gtest_executable(nwb_mesh_kernel_tests)
target_sources(nwb_mesh_kernel_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/object_raster_kernel_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/assets/object_raster_ps.slang"
    "${CMAKE_CURRENT_LIST_DIR}/object_geometry_kernel_fixture.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/object_geometry_kernel_fixture.h"
    "${CMAKE_CURRENT_LIST_DIR}/assets/object_transform_cs.slang"
    "${CMAKE_CURRENT_LIST_DIR}/mesh_kernel_fixture.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/mesh_kernel_fixture.h"
    "${CMAKE_CURRENT_LIST_DIR}/mesh_emulation_kernel_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/assets/custom_clip_ms.slang"
    "${CMAKE_CURRENT_LIST_DIR}/reference/mesh/authoring.slangi"
    "${CMAKE_CURRENT_LIST_DIR}/reference/mesh/entrypoints.slangi"
    "${CMAKE_CURRENT_LIST_DIR}/../descriptor_buffer/round_trip/round_trip_fixture.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../descriptor_buffer/round_trip/round_trip_fixture.h"
)
target_compile_definitions(nwb_mesh_kernel_tests PRIVATE
    NWB_COOK=1
    NWB_MESH_KERNEL_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
    NWB_MESH_KERNEL_OUTPUT_ROOT="${CMAKE_CURRENT_BINARY_DIR}/kernels/$<CONFIG>"
)
target_link_libraries(nwb_mesh_kernel_tests PRIVATE
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
set_tests_properties(nwb_mesh_kernel_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 300)
set_source_files_properties(
    "${CMAKE_CURRENT_LIST_DIR}/assets/object_raster_ps.slang"
    "${CMAKE_CURRENT_LIST_DIR}/assets/object_transform_cs.slang"
    "${CMAKE_CURRENT_LIST_DIR}/assets/custom_clip_ms.slang"
    "${CMAKE_CURRENT_LIST_DIR}/reference/mesh/authoring.slangi"
    "${CMAKE_CURRENT_LIST_DIR}/reference/mesh/entrypoints.slangi"
    PROPERTIES HEADER_FILE_ONLY TRUE
)
