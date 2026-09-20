if(NOT TARGET nwb_ecs_render)
    return()
endif()

nwb_declare_gtest_executable(nwb_material_resource_tests)
target_sources(nwb_material_resource_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/material_fixture_lifecycle_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../descriptor_buffer/round_trip/round_trip_fixture.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../descriptor_buffer/round_trip/round_trip_fixture.h"
)
target_link_libraries(nwb_material_resource_tests PRIVATE
    nwb_frame
    nwb_graphics
    nwb_perf
    nwb_alloc
    nwb_common
    nwb_assets_graphics
    nwb_ecs_ui
    nwb_ecs_render
)
set_tests_properties(nwb_material_resource_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 180)
