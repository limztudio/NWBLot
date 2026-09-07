nwb_declare_gtest_executable(nwb_ecs_model_tests)
target_sources(nwb_ecs_model_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/model_attachment_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/model_runtime_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/model_test_context.h"
)
target_link_libraries(nwb_ecs_model_tests PRIVATE
    nwb_ecs_model
    nwb_assets_model
    nwb_ecs_scene
    nwb_common
    nwb_alloc
)
