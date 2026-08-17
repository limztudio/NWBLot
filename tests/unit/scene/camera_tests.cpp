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

    SIMDVector tanHalfFov;
    EXPECT_TRUE(NWB::Impl::Scene::TryComputeCameraTanHalfVerticalFov(VectorReplicate(camera.verticalFovRadians()), tanHalfFov));
    EXPECT_GT(VectorGetX(tanHalfFov), 0.0f);
    EXPECT_NEAR(VectorGetX(tanHalfFov), 0.577350269f, 0.0001f);
    EXPECT_TRUE(NWB::Impl::Scene::CameraClipRangeValid(VectorReplicate(camera.nearPlane()), VectorReplicate(camera.farPlane())));
    EXPECT_EQ(VectorGetX(NWB::Impl::Scene::ResolveCameraAspectRatio(VectorReplicate(camera.aspectRatio()), VectorReplicate(1.5f))), 1.5f);

    camera.setAspectRatio(2.0f);
    EXPECT_EQ(VectorGetX(NWB::Impl::Scene::ResolveCameraAspectRatio(VectorReplicate(camera.aspectRatio()), VectorReplicate(1.5f))), 2.0f);

    NWB::Impl::Scene::CameraProjection projection;
    EXPECT_TRUE(NWB::Impl::Scene::TryBuildCameraProjection(
        VectorReplicate(camera.verticalFovRadians()),
        VectorReplicate(camera.nearPlane()),
        VectorReplicate(camera.farPlane()),
        VectorReplicate(camera.aspectRatio()),
        VectorReplicate(1.5f),
        projection
    ));
    const SIMDVector projectionParams = LoadFloat(projection.projectionParams);
    EXPECT_GT(VectorGetX(projectionParams), 0.0f);
    EXPECT_GT(VectorGetY(projectionParams), 0.0f);
    EXPECT_GT(VectorGetZ(projectionParams), 0.0f);
    EXPECT_LT(VectorGetW(projectionParams), 0.0f);
    const f32 depthRange = camera.farPlane() - camera.nearPlane();
    EXPECT_FLOAT_EQ(VectorGetX(projectionParams), 1.0f / (VectorGetX(tanHalfFov) * camera.aspectRatio()));
    EXPECT_FLOAT_EQ(VectorGetY(projectionParams), 1.0f / VectorGetX(tanHalfFov));
    EXPECT_FLOAT_EQ(VectorGetZ(projectionParams), camera.farPlane() / depthRange);
    EXPECT_FLOAT_EQ(VectorGetW(projectionParams), -(camera.nearPlane() * camera.farPlane()) / depthRange);
    EXPECT_TRUE(NWB::Impl::Scene::CameraProjectionStorageValid(projection));
    EXPECT_EQ(projection.aspectRatio, 2.0f);
    EXPECT_GT(projection.tanHalfVerticalFov, 0.0f);
    EXPECT_EQ(projection.nearPlane, camera.nearPlane());
    EXPECT_EQ(projection.farPlane, camera.farPlane());

    const NWB::Impl::Scene::CameraProjection defaultProjection = NWB::Impl::Scene::BuildDefaultCameraProjection(1.5f);
    EXPECT_TRUE(NWB::Impl::Scene::CameraProjectionStorageValid(defaultProjection));
    EXPECT_EQ(defaultProjection.aspectRatio, 1.5f);

    camera.setNearPlane(0.0f);
    EXPECT_FALSE(NWB::Impl::Scene::CameraClipRangeValid(VectorReplicate(camera.nearPlane()), VectorReplicate(camera.farPlane())));
    EXPECT_FALSE(NWB::Impl::Scene::TryBuildCameraProjection(
        VectorReplicate(camera.verticalFovRadians()),
        VectorReplicate(camera.nearPlane()),
        VectorReplicate(camera.farPlane()),
        VectorReplicate(camera.aspectRatio()),
        VectorReplicate(1.5f),
        projection
    ));
    const NWB::Impl::Scene::CameraProjection emptyProjection{};
    EXPECT_FALSE(NWB::Impl::Scene::CameraProjectionStorageValid(emptyProjection));

    camera = NWB::Impl::Scene::CameraComponent{};
    camera.setNearPlane(2.0f);
    camera.setFarPlane(s_MaxF32);
    EXPECT_TRUE(NWB::Impl::Scene::CameraClipRangeValid(VectorReplicate(camera.nearPlane()), VectorReplicate(camera.farPlane())));
    EXPECT_FALSE(NWB::Impl::Scene::TryBuildCameraProjection(
        VectorReplicate(camera.verticalFovRadians()),
        VectorReplicate(camera.nearPlane()),
        VectorReplicate(camera.farPlane()),
        VectorReplicate(camera.aspectRatio()),
        VectorReplicate(1.5f),
        projection
    ));

    camera = NWB::Impl::Scene::CameraComponent{};
    camera.setVerticalFovRadians(179.0f * (s_PI / 180.0f));
    EXPECT_FALSE(NWB::Impl::Scene::TryBuildCameraProjection(
        VectorReplicate(camera.verticalFovRadians()),
        VectorReplicate(camera.nearPlane()),
        VectorReplicate(camera.farPlane()),
        VectorReplicate(camera.aspectRatio()),
        VectorReplicate(s_MaxF32),
        projection
    ));

    camera.setAspectRatio(s_MaxF32);
    EXPECT_FALSE(NWB::Impl::Scene::TryBuildCameraProjection(
        VectorReplicate(camera.verticalFovRadians()),
        VectorReplicate(camera.nearPlane()),
        VectorReplicate(camera.farPlane()),
        VectorReplicate(camera.aspectRatio()),
        VectorReplicate(1.5f),
        projection
    ));

    camera = NWB::Impl::Scene::CameraComponent{};
    camera.setVerticalFovRadians(180.0f * (s_PI / 180.0f));
    EXPECT_FALSE(NWB::Impl::Scene::TryComputeCameraTanHalfVerticalFov(VectorReplicate(camera.verticalFovRadians()), tanHalfFov));
    camera.setVerticalFovRadians(400.0f * (s_PI / 180.0f));
    EXPECT_FALSE(NWB::Impl::Scene::TryComputeCameraTanHalfVerticalFov(VectorReplicate(camera.verticalFovRadians()), tanHalfFov));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

