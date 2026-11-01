// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "camera.h"

#include <core/ecs/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_SCENE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_camera{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<SceneCameraView> TryBuildSceneCameraView(
    const Core::ECS::EntityID entity,
    const f32 fallbackAspectRatio,
    const SIMDVectorSingle position,
    const SIMDVectorSingle rotation,
    const SIMDVectorSingle scale,
    TransformComponent& transform,
    CameraComponent& camera
)noexcept{
    const SIMDVectorSingle rotationLengthSquared = QuaternionLengthSq(rotation);
    if(
        !Vector3IsFinite(position)
        || QuaternionIsNaN(rotation)
        || QuaternionIsInfinite(rotation)
        || !Vector3IsFinite(scale)
        || !VectorIsFinite(rotationLengthSquared, VectorComponentMask::s_XYZW)
        || !Vector4NearEqual(rotationLengthSquared, s_SIMDOne, VectorReplicate(s_CameraRotationUnitLengthSquaredTolerance))
    )
        return MakeUnexpected(Failure{});

    const auto projection = TryBuildCameraProjection(VectorReplicate(camera.verticalFovRadians()), VectorReplicate(camera.nearPlane()), VectorReplicate(camera.farPlane()), VectorReplicate(camera.aspectRatio()), VectorReplicate(fallbackAspectRatio));
    if(!projection)
        return MakeUnexpected(Failure{});

    return SceneCameraView{ entity, &transform, &camera, StoreCameraProjection(*projection) };
}

[[nodiscard]] Core::ECS::EntityID ResolveActiveCamera(Core::ECS::World& world)noexcept{
    const auto activeCameraView = world.view<ActiveCameraComponent>();
    for(auto it = activeCameraView.begin(); it != activeCameraView.end(); ++it){
        [[maybe_unused]] auto&& [entity, activeCamera] = *it;
        return activeCamera.camera;
    }

    return Core::ECS::s_InvalidEntityId;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<SIMDVectorSingleResult> TryComputeCameraTanHalfVerticalFov(const SIMDVectorSingle verticalFovRadians)noexcept{
    if(!VectorIsFinite(verticalFovRadians, VectorComponentMask::s_XYZW) || !Vector4Greater(verticalFovRadians, s_SIMDZero) || !Vector4Less(verticalFovRadians, VectorReplicate(s_PI)))
        return MakeUnexpected(Failure{});

    SIMDVectorSingle sinHalfFovVector;
    SIMDVectorSingle cosHalfFovVector;

    VectorSinCos(sinHalfFovVector, cosHalfFovVector, VectorScale(verticalFovRadians, s_HalfFovWeight));
    if(!VectorIsFinite(sinHalfFovVector, VectorComponentMask::s_XYZW) || !VectorIsFinite(cosHalfFovVector, VectorComponentMask::s_XYZW) || !Vector4GreaterOrEqual(VectorAbs(cosHalfFovVector), VectorReplicate(s_CameraFovCosEpsilon)))
        return MakeUnexpected(Failure{});

    const SIMDVectorSingle tanHalfFovVector = VectorDivide(sinHalfFovVector, cosHalfFovVector);
    if(!VectorIsFinite(tanHalfFovVector, VectorComponentMask::s_XYZW) || !Vector4Greater(tanHalfFovVector, VectorZero()))
        return MakeUnexpected(Failure{});

    return SIMDVectorSingleResult{ tanHalfFovVector };
}

SIMDVectorSingle ResolveCameraAspectRatio(const SIMDVectorSingle cameraAspectRatio, const SIMDVectorSingle fallbackAspectRatio)noexcept{
    const SIMDVectorSingle fallbackValid = VectorAndCInt(VectorGreater(fallbackAspectRatio, s_SIMDZero), VectorOrInt(VectorIsNaN(fallbackAspectRatio), VectorIsInfinite(fallbackAspectRatio)));
    const SIMDVectorSingle resolvedFallbackAspectRatio = VectorSelect(s_SIMDOne, fallbackAspectRatio, fallbackValid);
    const SIMDVectorSingle cameraValid = VectorAndCInt(VectorGreater(cameraAspectRatio, s_SIMDZero), VectorOrInt(VectorIsNaN(cameraAspectRatio), VectorIsInfinite(cameraAspectRatio)));
    return VectorSelect(resolvedFallbackAspectRatio, cameraAspectRatio, cameraValid);
}

b8 CameraProjectionValuesValid(const SIMDVectorSingle projectionParams, const SIMDVectorSingle aspectRatio, const SIMDVectorSingle tanHalfVerticalFov, const SIMDVectorSingle nearPlane, const SIMDVectorSingle farPlane)noexcept{
    const SIMDVectorSingle validProjectionParams = VectorOrInt(VectorAndInt(VectorGreater(projectionParams, s_SIMDZero), s_SIMDMask3), VectorAndInt(VectorLess(projectionParams, s_SIMDZero), s_SIMDMaskW));
    return
        !Vector4IsNaN(projectionParams)
        && !Vector4IsInfinite(projectionParams)
        && !Vector4IsNaN(aspectRatio)
        && !Vector4IsInfinite(aspectRatio)
        && !Vector4IsNaN(tanHalfVerticalFov)
        && !Vector4IsInfinite(tanHalfVerticalFov)
        && Vector4Greater(aspectRatio, s_SIMDZero)
        && Vector4Greater(tanHalfVerticalFov, s_SIMDZero)
        && CameraClipRangeValid(nearPlane, farPlane)
        && VectorMoveMask(validProjectionParams) == VectorComponentMask::s_XYZW
    ;
}

Expected<SIMDMatrix> TryBuildCameraProjection(
    const SIMDVectorSingle verticalFovRadians,
    const SIMDVectorSingle nearPlane,
    const SIMDVectorSingle farPlane,
    const SIMDVectorSingle cameraAspectRatio,
    const SIMDVectorSingle fallbackAspectRatio
)noexcept{
    const auto tanHalfFov = TryComputeCameraTanHalfVerticalFov(verticalFovRadians);
    if(!tanHalfFov || !CameraClipRangeValid(nearPlane, farPlane))
        return MakeUnexpected(Failure{});

    const SIMDVectorSingle aspectRatio = ResolveCameraAspectRatio(cameraAspectRatio, fallbackAspectRatio);
    const SIMDVectorSingle depthRange = VectorSubtract(farPlane, nearPlane);
    const SIMDVectorSingle selectZW = VectorOrInt(s_SIMDMaskZ, s_SIMDMaskW);
    const SIMDVectorSingle projectionDenominators = VectorMultiply(VectorSelect(tanHalfFov->value, depthRange, selectZW), VectorSelect(s_SIMDOne, aspectRatio, s_SIMDMaskX));
    const SIMDVectorSingle projectionNumerators = VectorMultiply(VectorSelect(s_SIMDOne, VectorMergeXY(farPlane, nearPlane), selectZW), VectorSelect(s_SIMDOne, VectorNegate(farPlane), s_SIMDMaskW));
    const SIMDVectorSingle projectionParams = VectorDivide(projectionNumerators, projectionDenominators);
    if(!CameraProjectionValuesValid(projectionParams, aspectRatio, tanHalfFov->value, nearPlane, farPlane))
        return MakeUnexpected(Failure{});

    return SIMDMatrix{ .v = { projectionParams, VectorMergeX(aspectRatio, tanHalfFov->value, nearPlane, farPlane), VectorZero(), VectorZero() } };
}

CameraProjection StoreCameraProjection(const SIMDMatrix& values)noexcept{
    CameraProjection projection;
    StoreFloat(values.v[0], projection.projectionParams);
    projection.aspectRatio = VectorGetX(values.v[1]);
    projection.tanHalfVerticalFov = VectorGetY(values.v[1]);
    projection.nearPlane = VectorGetZ(values.v[1]);
    projection.farPlane = VectorGetW(values.v[1]);
    return projection;
}

CameraProjection StoreDefaultCameraProjection(const f32 fallbackAspectRatio)noexcept{
    const auto defaultProjection = TryBuildCameraProjection(VectorReplicate(s_CameraVerticalFovRadians), VectorReplicate(s_CameraNearPlane), VectorReplicate(s_CameraFarPlane), VectorReplicate(s_CameraAutoAspectRatio), VectorReplicate(fallbackAspectRatio));
    if(defaultProjection)
        return StoreCameraProjection(*defaultProjection);

    CameraProjection projection;
    projection.projectionParams = Float4(1.0f, 1.0f, 1.0f, 0.0f);
    projection.aspectRatio = 1.0f;
    projection.tanHalfVerticalFov = 1.0f;
    projection.nearPlane = s_CameraNearPlane;
    projection.farPlane = s_CameraFarPlane;
    return projection;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 SceneCameraView::valid()const noexcept{
    return
        entity.valid()
        && transform != nullptr
        && camera != nullptr
        && CameraProjectionValuesValid(
            LoadFloat(projection.projectionParams),
            VectorReplicate(projection.aspectRatio),
            VectorReplicate(projection.tanHalfVerticalFov),
            VectorReplicate(projection.nearPlane),
            VectorReplicate(projection.farPlane)
        )
    ;
}

SceneCameraView ResolveSceneCameraView(Core::ECS::World& world, const f32 fallbackAspectRatio)noexcept{
    const Core::ECS::EntityID activeCamera = __hidden_camera::ResolveActiveCamera(world);
    if(activeCamera.valid()){
        auto* transform = world.tryGetComponent<TransformComponent>(activeCamera);
        auto* camera = world.tryGetComponent<CameraComponent>(activeCamera);
        if(transform && camera){
            const auto requestedCamera = __hidden_camera::TryBuildSceneCameraView(activeCamera, fallbackAspectRatio, LoadFloat(transform->position), LoadFloat(transform->rotation), LoadFloat(transform->scale), *transform, *camera);
            if(requestedCamera)
                return *requestedCamera;
        }
    }

    const auto cameraView = world.view<TransformComponent, CameraComponent>();
    for(auto it = cameraView.begin(); it != cameraView.end(); ++it){
        auto&& [entity, transform, camera] = *it;
        const auto resolvedCamera = __hidden_camera::TryBuildSceneCameraView(entity, fallbackAspectRatio, LoadFloat(transform.position), LoadFloat(transform.rotation), LoadFloat(transform.scale), transform, camera);
        if(resolvedCamera)
            return *resolvedCamera;
    }

    return {};
}

Core::ECS::EntityID CreateSceneCameraEntity(Core::ECS::World& world, const Float4& position){
    auto cameraEntity = world.createEntity();
    auto& transform = cameraEntity.addComponent<TransformComponent>();
    transform.position = position;
    cameraEntity.addComponent<CameraComponent>();
    return cameraEntity.id();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_SCENE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

