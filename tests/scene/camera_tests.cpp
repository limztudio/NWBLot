// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_scene/module.h>

#include <core/common/module.h>

#include <tests/test_context.h>

#include <global/compile.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_scene_camera_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestContext = NWB::Tests::TestContext;


#define NWB_SCENE_CAMERA_TEST_CHECK NWB_TEST_CHECK


static void TestCameraProjectionHelpers(TestContext& context){
    NWB::Impl::Scene::CameraComponent camera;

    f32 tanHalfFov = 0.0f;
    NWB_SCENE_CAMERA_TEST_CHECK(
        context,
        NWB::Impl::Scene::TryComputeCameraTanHalfVerticalFov(camera.verticalFovRadians(), tanHalfFov)
    );
    NWB_SCENE_CAMERA_TEST_CHECK(context, tanHalfFov > 0.0f);
    NWB_SCENE_CAMERA_TEST_CHECK(context, NWB::Impl::Scene::CameraClipRangeValid(camera));
    NWB_SCENE_CAMERA_TEST_CHECK(context, NWB::Impl::Scene::ResolveCameraAspectRatio(camera, 1.5f) == 1.5f);

    camera.setAspectRatio(2.0f);
    NWB_SCENE_CAMERA_TEST_CHECK(context, NWB::Impl::Scene::ResolveCameraAspectRatio(camera, 1.5f) == 2.0f);

    Float4 projectionParams;
    NWB_SCENE_CAMERA_TEST_CHECK(context, NWB::Impl::Scene::TryBuildCameraProjectionParams(camera, 1.5f, projectionParams));
    NWB_SCENE_CAMERA_TEST_CHECK(context, projectionParams.x > 0.0f);
    NWB_SCENE_CAMERA_TEST_CHECK(context, projectionParams.y > 0.0f);
    NWB_SCENE_CAMERA_TEST_CHECK(context, projectionParams.z > 0.0f);
    NWB_SCENE_CAMERA_TEST_CHECK(context, projectionParams.w < 0.0f);

    NWB::Impl::Scene::CameraProjectionData projectionData;
    NWB_SCENE_CAMERA_TEST_CHECK(context, NWB::Impl::Scene::TryBuildCameraProjectionData(camera, 1.5f, projectionData));
    NWB_SCENE_CAMERA_TEST_CHECK(
        context,
        NWB::Impl::Scene::CameraProjectionDataValid(
            LoadFloat(projectionData.projectionParams),
            projectionData.aspectRatio,
            projectionData.tanHalfVerticalFov
        )
    );
    NWB_SCENE_CAMERA_TEST_CHECK(context, projectionData.projectionParams.x == projectionParams.x);
    NWB_SCENE_CAMERA_TEST_CHECK(context, projectionData.projectionParams.y == projectionParams.y);
    NWB_SCENE_CAMERA_TEST_CHECK(context, projectionData.projectionParams.z == projectionParams.z);
    NWB_SCENE_CAMERA_TEST_CHECK(context, projectionData.projectionParams.w == projectionParams.w);
    NWB_SCENE_CAMERA_TEST_CHECK(context, projectionData.aspectRatio == 2.0f);
    NWB_SCENE_CAMERA_TEST_CHECK(context, projectionData.tanHalfVerticalFov > 0.0f);

    camera.setNearPlane(0.0f);
    NWB_SCENE_CAMERA_TEST_CHECK(context, !NWB::Impl::Scene::CameraClipRangeValid(camera));
    NWB_SCENE_CAMERA_TEST_CHECK(context, !NWB::Impl::Scene::TryBuildCameraProjectionParams(camera, 1.5f, projectionParams));
    const NWB::Impl::Scene::CameraProjectionData emptyProjectionData{};
    NWB_SCENE_CAMERA_TEST_CHECK(
        context,
        !NWB::Impl::Scene::CameraProjectionDataValid(
            LoadFloat(emptyProjectionData.projectionParams),
            emptyProjectionData.aspectRatio,
            emptyProjectionData.tanHalfVerticalFov
        )
    );

    camera = NWB::Impl::Scene::CameraComponent{};
    camera.setNearPlane(2.0f);
    camera.setFarPlane(s_MaxF32);
    NWB_SCENE_CAMERA_TEST_CHECK(context, NWB::Impl::Scene::CameraClipRangeValid(camera));
    NWB_SCENE_CAMERA_TEST_CHECK(context, !NWB::Impl::Scene::TryBuildCameraProjectionParams(camera, 1.5f, projectionParams));

    camera = NWB::Impl::Scene::CameraComponent{};
    camera.setVerticalFovRadians(179.0f * (s_PI / 180.0f));
    NWB_SCENE_CAMERA_TEST_CHECK(context, !NWB::Impl::Scene::TryBuildCameraProjectionParams(camera, s_MaxF32, projectionParams));

    camera.setAspectRatio(s_MaxF32);
    NWB_SCENE_CAMERA_TEST_CHECK(context, !NWB::Impl::Scene::TryBuildCameraProjectionData(camera, 1.5f, projectionData));

    camera = NWB::Impl::Scene::CameraComponent{};
    camera.setVerticalFovRadians(180.0f * (s_PI / 180.0f));
    NWB_SCENE_CAMERA_TEST_CHECK(
        context,
        !NWB::Impl::Scene::TryComputeCameraTanHalfVerticalFov(camera.verticalFovRadians(), tanHalfFov)
    );
    camera.setVerticalFovRadians(400.0f * (s_PI / 180.0f));
    NWB_SCENE_CAMERA_TEST_CHECK(
        context,
        !NWB::Impl::Scene::TryComputeCameraTanHalfVerticalFov(camera.verticalFovRadians(), tanHalfFov)
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#undef NWB_SCENE_CAMERA_TEST_CHECK


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Scene, CameraProjectionHelpers){
    NWB::Tests::TestContext nwbTestContext;
    __hidden_scene_camera_tests::TestCameraProjectionHelpers(nwbTestContext);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

