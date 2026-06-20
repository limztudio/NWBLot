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


static void TestCameraProjectionHelpers(){
    NWB::Impl::Scene::CameraComponent camera;

    f32 tanHalfFov = 0.0f;
    EXPECT_TRUE((NWB::Impl::Scene::TryComputeCameraTanHalfVerticalFov(camera.verticalFovRadians(), tanHalfFov)));
    EXPECT_TRUE((tanHalfFov > 0.0f));
    EXPECT_TRUE((NWB::Impl::Scene::CameraClipRangeValid(camera)));
    EXPECT_TRUE((NWB::Impl::Scene::ResolveCameraAspectRatio(camera, 1.5f) == 1.5f));

    camera.setAspectRatio(2.0f);
    EXPECT_TRUE((NWB::Impl::Scene::ResolveCameraAspectRatio(camera, 1.5f) == 2.0f));

    Float4 projectionParams;
    EXPECT_TRUE((NWB::Impl::Scene::TryBuildCameraProjectionParams(camera, 1.5f, projectionParams)));
    EXPECT_TRUE((projectionParams.x > 0.0f));
    EXPECT_TRUE((projectionParams.y > 0.0f));
    EXPECT_TRUE((projectionParams.z > 0.0f));
    EXPECT_TRUE((projectionParams.w < 0.0f));

    NWB::Impl::Scene::CameraProjectionData projectionData;
    EXPECT_TRUE((NWB::Impl::Scene::TryBuildCameraProjectionData(camera, 1.5f, projectionData)));
    EXPECT_TRUE((NWB::Impl::Scene::CameraProjectionDataValid(
            LoadFloat(projectionData.projectionParams),
            projectionData.aspectRatio,
            projectionData.tanHalfVerticalFov
        )));
    EXPECT_TRUE((projectionData.projectionParams.x == projectionParams.x));
    EXPECT_TRUE((projectionData.projectionParams.y == projectionParams.y));
    EXPECT_TRUE((projectionData.projectionParams.z == projectionParams.z));
    EXPECT_TRUE((projectionData.projectionParams.w == projectionParams.w));
    EXPECT_TRUE((projectionData.aspectRatio == 2.0f));
    EXPECT_TRUE((projectionData.tanHalfVerticalFov > 0.0f));

    camera.setNearPlane(0.0f);
    EXPECT_TRUE((!NWB::Impl::Scene::CameraClipRangeValid(camera)));
    EXPECT_TRUE((!NWB::Impl::Scene::TryBuildCameraProjectionParams(camera, 1.5f, projectionParams)));
    const NWB::Impl::Scene::CameraProjectionData emptyProjectionData{};
    EXPECT_TRUE((!NWB::Impl::Scene::CameraProjectionDataValid(
            LoadFloat(emptyProjectionData.projectionParams),
            emptyProjectionData.aspectRatio,
            emptyProjectionData.tanHalfVerticalFov
        )));

    camera = NWB::Impl::Scene::CameraComponent{};
    camera.setNearPlane(2.0f);
    camera.setFarPlane(s_MaxF32);
    EXPECT_TRUE((NWB::Impl::Scene::CameraClipRangeValid(camera)));
    EXPECT_TRUE((!NWB::Impl::Scene::TryBuildCameraProjectionParams(camera, 1.5f, projectionParams)));

    camera = NWB::Impl::Scene::CameraComponent{};
    camera.setVerticalFovRadians(179.0f * (s_PI / 180.0f));
    EXPECT_TRUE((!NWB::Impl::Scene::TryBuildCameraProjectionParams(camera, s_MaxF32, projectionParams)));

    camera.setAspectRatio(s_MaxF32);
    EXPECT_TRUE((!NWB::Impl::Scene::TryBuildCameraProjectionData(camera, 1.5f, projectionData)));

    camera = NWB::Impl::Scene::CameraComponent{};
    camera.setVerticalFovRadians(180.0f * (s_PI / 180.0f));
    EXPECT_TRUE((!NWB::Impl::Scene::TryComputeCameraTanHalfVerticalFov(camera.verticalFovRadians(), tanHalfFov)));
    camera.setVerticalFovRadians(400.0f * (s_PI / 180.0f));
    EXPECT_TRUE((!NWB::Impl::Scene::TryComputeCameraTanHalfVerticalFov(camera.verticalFovRadians(), tanHalfFov)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Scene, CameraProjectionHelpers){
    __hidden_scene_camera_tests::TestCameraProjectionHelpers();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

