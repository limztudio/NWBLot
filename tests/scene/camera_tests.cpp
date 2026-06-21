// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_scene/module.h>

#include <core/common/module.h>

#include <global/compile.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_scene_camera_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Scene, CameraProjectionHelpers){
    NWB::Impl::Scene::CameraComponent camera;

    f32 tanHalfFov = 0.0f;
    EXPECT_TRUE(NWB::Impl::Scene::TryComputeCameraTanHalfVerticalFov(camera.verticalFovRadians(), tanHalfFov));
    EXPECT_GT(tanHalfFov, 0.0f);
    EXPECT_TRUE(NWB::Impl::Scene::CameraClipRangeValid(camera));
    EXPECT_EQ(NWB::Impl::Scene::ResolveCameraAspectRatio(camera, 1.5f), 1.5f);

    camera.setAspectRatio(2.0f);
    EXPECT_EQ(NWB::Impl::Scene::ResolveCameraAspectRatio(camera, 1.5f), 2.0f);

    Float4 projectionParams;
    EXPECT_TRUE(NWB::Impl::Scene::TryBuildCameraProjectionParams(camera, 1.5f, projectionParams));
    EXPECT_GT(projectionParams.x, 0.0f);
    EXPECT_GT(projectionParams.y, 0.0f);
    EXPECT_GT(projectionParams.z, 0.0f);
    EXPECT_LT(projectionParams.w, 0.0f);

    NWB::Impl::Scene::CameraProjectionData projectionData;
    EXPECT_TRUE(NWB::Impl::Scene::TryBuildCameraProjectionData(camera, 1.5f, projectionData));
    EXPECT_TRUE(NWB::Impl::Scene::CameraProjectionDataValid(
            LoadFloat(projectionData.projectionParams),
            projectionData.aspectRatio,
            projectionData.tanHalfVerticalFov
        ));
    EXPECT_EQ(projectionData.projectionParams.x, projectionParams.x);
    EXPECT_EQ(projectionData.projectionParams.y, projectionParams.y);
    EXPECT_EQ(projectionData.projectionParams.z, projectionParams.z);
    EXPECT_EQ(projectionData.projectionParams.w, projectionParams.w);
    EXPECT_EQ(projectionData.aspectRatio, 2.0f);
    EXPECT_GT(projectionData.tanHalfVerticalFov, 0.0f);

    camera.setNearPlane(0.0f);
    EXPECT_FALSE(NWB::Impl::Scene::CameraClipRangeValid(camera));
    EXPECT_FALSE(NWB::Impl::Scene::TryBuildCameraProjectionParams(camera, 1.5f, projectionParams));
    const NWB::Impl::Scene::CameraProjectionData emptyProjectionData{};
    EXPECT_FALSE(NWB::Impl::Scene::CameraProjectionDataValid(
            LoadFloat(emptyProjectionData.projectionParams),
            emptyProjectionData.aspectRatio,
            emptyProjectionData.tanHalfVerticalFov
        ));

    camera = NWB::Impl::Scene::CameraComponent{};
    camera.setNearPlane(2.0f);
    camera.setFarPlane(s_MaxF32);
    EXPECT_TRUE(NWB::Impl::Scene::CameraClipRangeValid(camera));
    EXPECT_FALSE(NWB::Impl::Scene::TryBuildCameraProjectionParams(camera, 1.5f, projectionParams));

    camera = NWB::Impl::Scene::CameraComponent{};
    camera.setVerticalFovRadians(179.0f * (s_PI / 180.0f));
    EXPECT_FALSE(NWB::Impl::Scene::TryBuildCameraProjectionParams(camera, s_MaxF32, projectionParams));

    camera.setAspectRatio(s_MaxF32);
    EXPECT_FALSE(NWB::Impl::Scene::TryBuildCameraProjectionData(camera, 1.5f, projectionData));

    camera = NWB::Impl::Scene::CameraComponent{};
    camera.setVerticalFovRadians(180.0f * (s_PI / 180.0f));
    EXPECT_FALSE(NWB::Impl::Scene::TryComputeCameraTanHalfVerticalFov(camera.verticalFovRadians(), tanHalfFov));
    camera.setVerticalFovRadians(400.0f * (s_PI / 180.0f));
    EXPECT_FALSE(NWB::Impl::Scene::TryComputeCameraTanHalfVerticalFov(camera.verticalFovRadians(), tanHalfFov));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

