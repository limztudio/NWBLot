// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_camera/ecs_camera.h>

#include <core/common/common.h>

#include <tests/test_context.h>

#include <global/compile.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_camera_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestContext = NWB::Tests::TestContext;


#define NWB_ECS_CAMERA_TEST_CHECK NWB_TEST_CHECK


static void TestCameraProjectionHelpers(TestContext& context){
    NWB::Core::ECSCamera::CameraComponent camera;

    f32 tanHalfFov = 0.0f;
    NWB_ECS_CAMERA_TEST_CHECK(
        context,
        NWB::Core::ECSCamera::TryComputeCameraTanHalfVerticalFov(camera.verticalFovRadians(), tanHalfFov)
    );
    NWB_ECS_CAMERA_TEST_CHECK(context, tanHalfFov > 0.0f);
    NWB_ECS_CAMERA_TEST_CHECK(context, NWB::Core::ECSCamera::CameraClipRangeValid(camera));
    NWB_ECS_CAMERA_TEST_CHECK(context, NWB::Core::ECSCamera::ResolveCameraAspectRatio(camera, 1.5f) == 1.5f);

    camera.setAspectRatio(2.0f);
    NWB_ECS_CAMERA_TEST_CHECK(context, NWB::Core::ECSCamera::ResolveCameraAspectRatio(camera, 1.5f) == 2.0f);

    Float4 projectionParams;
    NWB_ECS_CAMERA_TEST_CHECK(context, NWB::Core::ECSCamera::TryBuildCameraProjectionParams(camera, 1.5f, projectionParams));
    NWB_ECS_CAMERA_TEST_CHECK(context, projectionParams.x > 0.0f);
    NWB_ECS_CAMERA_TEST_CHECK(context, projectionParams.y > 0.0f);
    NWB_ECS_CAMERA_TEST_CHECK(context, projectionParams.z > 0.0f);
    NWB_ECS_CAMERA_TEST_CHECK(context, projectionParams.w < 0.0f);

    NWB::Core::ECSCamera::CameraProjectionData projectionData;
    NWB_ECS_CAMERA_TEST_CHECK(context, NWB::Core::ECSCamera::TryBuildCameraProjectionData(camera, 1.5f, projectionData));
    NWB_ECS_CAMERA_TEST_CHECK(context, NWB::Core::ECSCamera::CameraProjectionDataValid(projectionData));
    NWB_ECS_CAMERA_TEST_CHECK(context, projectionData.projectionParams.x == projectionParams.x);
    NWB_ECS_CAMERA_TEST_CHECK(context, projectionData.projectionParams.y == projectionParams.y);
    NWB_ECS_CAMERA_TEST_CHECK(context, projectionData.projectionParams.z == projectionParams.z);
    NWB_ECS_CAMERA_TEST_CHECK(context, projectionData.projectionParams.w == projectionParams.w);
    NWB_ECS_CAMERA_TEST_CHECK(context, projectionData.aspectRatio == 2.0f);
    NWB_ECS_CAMERA_TEST_CHECK(context, projectionData.tanHalfVerticalFov > 0.0f);

    camera.setNearPlane(0.0f);
    NWB_ECS_CAMERA_TEST_CHECK(context, !NWB::Core::ECSCamera::CameraClipRangeValid(camera));
    NWB_ECS_CAMERA_TEST_CHECK(context, !NWB::Core::ECSCamera::TryBuildCameraProjectionParams(camera, 1.5f, projectionParams));
    NWB_ECS_CAMERA_TEST_CHECK(
        context,
        !NWB::Core::ECSCamera::CameraProjectionDataValid(NWB::Core::ECSCamera::CameraProjectionData{})
    );

    camera = NWB::Core::ECSCamera::CameraComponent{};
    camera.setNearPlane(2.0f);
    camera.setFarPlane(s_MaxF32);
    NWB_ECS_CAMERA_TEST_CHECK(context, NWB::Core::ECSCamera::CameraClipRangeValid(camera));
    NWB_ECS_CAMERA_TEST_CHECK(context, !NWB::Core::ECSCamera::TryBuildCameraProjectionParams(camera, 1.5f, projectionParams));

    camera = NWB::Core::ECSCamera::CameraComponent{};
    camera.setVerticalFovRadians(179.0f * (s_PI / 180.0f));
    NWB_ECS_CAMERA_TEST_CHECK(context, !NWB::Core::ECSCamera::TryBuildCameraProjectionParams(camera, s_MaxF32, projectionParams));

    camera.setAspectRatio(s_MaxF32);
    NWB_ECS_CAMERA_TEST_CHECK(context, !NWB::Core::ECSCamera::TryBuildCameraProjectionData(camera, 1.5f, projectionData));

    camera = NWB::Core::ECSCamera::CameraComponent{};
    camera.setVerticalFovRadians(180.0f * (s_PI / 180.0f));
    NWB_ECS_CAMERA_TEST_CHECK(
        context,
        !NWB::Core::ECSCamera::TryComputeCameraTanHalfVerticalFov(camera.verticalFovRadians(), tanHalfFov)
    );
    camera.setVerticalFovRadians(400.0f * (s_PI / 180.0f));
    NWB_ECS_CAMERA_TEST_CHECK(
        context,
        !NWB::Core::ECSCamera::TryComputeCameraTanHalfVerticalFov(camera.verticalFovRadians(), tanHalfFov)
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#undef NWB_ECS_CAMERA_TEST_CHECK


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static int EntryPoint(const isize argc, tchar** argv, void*){
    static_cast<void>(argc);
    static_cast<void>(argv);

    return NWB::Tests::RunTestSuite("ecs camera", [](NWB::Tests::TestContext& context){
        __hidden_ecs_camera_tests::TestCameraProjectionHelpers(context);
    });
}


#include <core/common/application_entry.h>

NWB_DEFINE_APPLICATION_ENTRY_POINT(EntryPoint)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
