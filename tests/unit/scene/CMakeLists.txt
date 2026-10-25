nwb_declare_gtest_executable(nwb_ecs_scene_tests)
target_sources(nwb_ecs_scene_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/camera_selection_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/scene_subsystem_asset_tests.cpp"
)
target_link_libraries(nwb_ecs_scene_tests PRIVATE
    nwb_ecs_scene
    nwb_assets_scene
    nwb_assets_subsystem
    nwb_assets
    nwb_ecs
    nwb_common
    nwb_alloc
)
