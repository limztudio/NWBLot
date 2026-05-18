// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_camera/components.h>

#include <core/common/common.h>

#include <tests/test_context.h>

#include <global/compile.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_scene_camera_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestContext = NWB::Tests::TestContext;


#define NWB_SCENE_CAMERA_TEST_CHECK NWB_TEST_CHECK


static void TestCameraProjectionHelpers(TestContext& context){
    NWB::Impl::CameraComponent camera;

    f32 tanHalfFov = 0.0f;
    NWB_SCENE_CAMERA_TEST_CHECK(
        context,
        NWB::Impl::TryComputeCameraTanHalfVerticalFov(camera.verticalFovRadians(), tanHalfFov)
    );
    NWB_SCENE_CAMERA_TEST_CHECK(context, tanHalfFov > 0.0f);
    NWB_SCENE_CAMERA_TEST_CHECK(context, NWB::Impl::CameraClipRangeValid(camera));
    NWB_SCENE_CAMERA_TEST_CHECK(context, NWB::Impl::ResolveCameraAspectRatio(camera, 1.5f) == 1.5f);

    camera.setAspectRatio(2.0f);
    NWB_SCENE_CAMERA_TEST_CHECK(context, NWB::Impl::ResolveCameraAspectRatio(camera, 1.5f) == 2.0f);

    Float4 projectionParams;
    NWB_SCENE_CAMERA_TEST_CHECK(context, NWB::Impl::TryBuildCameraProjectionParams(camera, 1.5f, projectionParams));
    NWB_SCENE_CAMERA_TEST_CHECK(context, projectionParams.x > 0.0f);
    NWB_SCENE_CAMERA_TEST_CHECK(context, projectionParams.y > 0.0f);
    NWB_SCENE_CAMERA_TEST_CHECK(context, projectionParams.z > 0.0f);
    NWB_SCENE_CAMERA_TEST_CHECK(context, projectionParams.w < 0.0f);

    NWB::Impl::CameraProjectionData projectionData;
    NWB_SCENE_CAMERA_TEST_CHECK(context, NWB::Impl::TryBuildCameraProjectionData(camera, 1.5f, projectionData));
    NWB_SCENE_CAMERA_TEST_CHECK(context, NWB::Impl::CameraProjectionDataValid(projectionData));
    NWB_SCENE_CAMERA_TEST_CHECK(context, projectionData.projectionParams.x == projectionParams.x);
    NWB_SCENE_CAMERA_TEST_CHECK(context, projectionData.projectionParams.y == projectionParams.y);
    NWB_SCENE_CAMERA_TEST_CHECK(context, projectionData.projectionParams.z == projectionParams.z);
    NWB_SCENE_CAMERA_TEST_CHECK(context, projectionData.projectionParams.w == projectionParams.w);
    NWB_SCENE_CAMERA_TEST_CHECK(context, projectionData.aspectRatio == 2.0f);
    NWB_SCENE_CAMERA_TEST_CHECK(context, projectionData.tanHalfVerticalFov > 0.0f);

    camera.setNearPlane(0.0f);
    NWB_SCENE_CAMERA_TEST_CHECK(context, !NWB::Impl::CameraClipRangeValid(camera));
    NWB_SCENE_CAMERA_TEST_CHECK(context, !NWB::Impl::TryBuildCameraProjectionParams(camera, 1.5f, projectionParams));
    NWB_SCENE_CAMERA_TEST_CHECK(
        context,
        !NWB::Impl::CameraProjectionDataValid(NWB::Impl::CameraProjectionData{})
    );

    camera = NWB::Impl::CameraComponent{};
    camera.setNearPlane(2.0f);
    camera.setFarPlane(s_MaxF32);
    NWB_SCENE_CAMERA_TEST_CHECK(context, NWB::Impl::CameraClipRangeValid(camera));
    NWB_SCENE_CAMERA_TEST_CHECK(context, !NWB::Impl::TryBuildCameraProjectionParams(camera, 1.5f, projectionParams));

    camera = NWB::Impl::CameraComponent{};
    camera.setVerticalFovRadians(179.0f * (s_PI / 180.0f));
    NWB_SCENE_CAMERA_TEST_CHECK(context, !NWB::Impl::TryBuildCameraProjectionParams(camera, s_MaxF32, projectionParams));

    camera.setAspectRatio(s_MaxF32);
    NWB_SCENE_CAMERA_TEST_CHECK(context, !NWB::Impl::TryBuildCameraProjectionData(camera, 1.5f, projectionData));

    camera = NWB::Impl::CameraComponent{};
    camera.setVerticalFovRadians(180.0f * (s_PI / 180.0f));
    NWB_SCENE_CAMERA_TEST_CHECK(
        context,
        !NWB::Impl::TryComputeCameraTanHalfVerticalFov(camera.verticalFovRadians(), tanHalfFov)
    );
    camera.setVerticalFovRadians(400.0f * (s_PI / 180.0f));
    NWB_SCENE_CAMERA_TEST_CHECK(
        context,
        !NWB::Impl::TryComputeCameraTanHalfVerticalFov(camera.verticalFovRadians(), tanHalfFov)
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#undef NWB_SCENE_CAMERA_TEST_CHECK


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_TEST_ENTRY_POINT("scene camera", [](NWB::Tests::TestContext& context){
    __hidden_scene_camera_tests::TestCameraProjectionHelpers(context);
})


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

