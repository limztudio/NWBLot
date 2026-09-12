if(NOT TARGET nwb_ecs_render)
    return()
endif()

# Raytrace owner lifecycle tests reuse the shared validation-enabled headless descriptor-buffer fixture.
nwb_declare_gtest_executable(nwb_raytrace_tests)
target_sources(nwb_raytrace_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/optical_scene_upload_reuse_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../descriptor_buffer/round_trip/round_trip_fixture.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../descriptor_buffer/round_trip/round_trip_fixture.h"
)
target_link_libraries(nwb_raytrace_tests PRIVATE
    nwb_frame
    nwb_graphics
    nwb_perf
    nwb_alloc
    nwb_common
    nwb_assets_graphics
    nwb_ecs_ui
    nwb_ecs_render
)
set_tests_properties(nwb_raytrace_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 180)
