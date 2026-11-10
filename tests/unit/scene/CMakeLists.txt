nwb_declare_gtest_executable(nwb_ecs_scene_tests)
nwb_target_adhd_sources(nwb_ecs_scene_tests NAMESPACE "NWB::Tests::SceneAssetCases" SOURCES "${CMAKE_CURRENT_LIST_DIR}/*.adhd")
nwb_target_adhd_sources(nwb_ecs_scene_tests NAMESPACE "Testbed" SOURCES "${PROJECT_SOURCE_DIR}/CoolStuff/Testbed/fly_camera_motion.adhd")
nwb_target_link_libraries_whole_archive(nwb_ecs_scene_tests
    nwb_assets_graphics
    nwb_assets_mesh
    nwb_assets_ui_skin
    nwb_assets_subsystem
    nwb_assets_scene
)
target_link_libraries(nwb_ecs_scene_tests PRIVATE
    nwb_ecs_scene
    nwb_input
    nwb_assets
    nwb_ecs
    nwb_common
    nwb_alloc
)
