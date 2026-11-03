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
    TransformComponent& transform,
    CameraComponent& camera
)noexcept{
    if(!IsValidCameraTransformVectors(transform.position, transform.rotation, transform.scale))
        return MakeUnexpected(Failure{});

    const auto projection = TryBuildCameraProjectionData(
        camera.projection.x,
        camera.projection.y,
        camera.projection.z,
        camera.projection.w,
        fallbackAspectRatio
    );
    if(!projection)
        return MakeUnexpected(Failure{});

    return SceneCameraView{ entity, &transform, &camera, *projection };
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
    const auto tanHalfFov = TryComputeCameraTanHalfVerticalFovScalar(VectorGetX(verticalFovRadians));
    if(!tanHalfFov)
        return MakeUnexpected(Failure{});

    return SIMDVectorSingleResult{ VectorReplicate(*tanHalfFov) };
}

SIMDVectorSingle ResolveCameraAspectRatio(const SIMDVectorSingle cameraAspectRatio, const SIMDVectorSingle fallbackAspectRatio)noexcept{
    return VectorReplicate(ResolveCameraAspectRatioScalar(VectorGetX(cameraAspectRatio), VectorGetX(fallbackAspectRatio)));
}

b8 CameraProjectionValuesValid(const SIMDVectorSingle projectionParams, const SIMDVectorSingle aspectRatio, const SIMDVectorSingle tanHalfVerticalFov, const SIMDVectorSingle nearPlane, const SIMDVectorSingle farPlane)noexcept{
    Float4 params;
    StoreFloat(projectionParams, params);
    return CameraProjectionValuesValidScalar(params, VectorGetX(aspectRatio), VectorGetX(tanHalfVerticalFov), VectorGetX(nearPlane), VectorGetX(farPlane));
}

Expected<SIMDMatrix> TryBuildCameraProjection(
    const SIMDVectorSingle verticalFovRadians,
    const SIMDVectorSingle nearPlane,
    const SIMDVectorSingle farPlane,
    const SIMDVectorSingle cameraAspectRatio,
    const SIMDVectorSingle fallbackAspectRatio
)noexcept{
    const auto built = TryBuildCameraProjectionData(
        VectorGetX(verticalFovRadians),
        VectorGetX(nearPlane),
        VectorGetX(farPlane),
        VectorGetX(cameraAspectRatio),
        VectorGetX(fallbackAspectRatio)
    );
    if(!built)
        return MakeUnexpected(Failure{});

    return SIMDMatrix{ .v = {
        LoadFloat(built->projectionParams),
        VectorSet(built->aspectRatio, built->tanHalfVerticalFov, built->nearPlane, built->farPlane),
        VectorZero(),
        VectorZero()
    } };
}

CameraProjection StoreCameraProjection(const SIMDMatrix& values)noexcept{
    Float4 projectionParams;
    Float4 meta;
    StoreFloat(values.v[0], projectionParams);
    StoreFloat(values.v[1], meta);
    return MakeCameraProjection(projectionParams, meta);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 SceneCameraView::valid()const noexcept{
    return
        entity.valid()
        && transform != nullptr
        && camera != nullptr
        && CameraProjectionValuesValidScalar(
            projection.projectionParams,
            projection.aspectRatio,
            projection.tanHalfVerticalFov,
            projection.nearPlane,
            projection.farPlane
        )
    ;
}

SceneCameraView ResolveSceneCameraView(Core::ECS::World& world, const f32 fallbackAspectRatio)noexcept{
    const Core::ECS::EntityID activeCamera = __hidden_camera::ResolveActiveCamera(world);
    if(activeCamera.valid()){
        auto* transform = world.tryGetComponent<TransformComponent>(activeCamera);
        auto* camera = world.tryGetComponent<CameraComponent>(activeCamera);
        if(transform && camera){
            const auto requestedCamera = __hidden_camera::TryBuildSceneCameraView(activeCamera, fallbackAspectRatio, *transform, *camera);
            if(requestedCamera)
                return *requestedCamera;
        }
    }

    const auto cameraView = world.view<TransformComponent, CameraComponent>();
    for(auto it = cameraView.begin(); it != cameraView.end(); ++it){
        auto&& [entity, transform, camera] = *it;
        const auto resolvedCamera = __hidden_camera::TryBuildSceneCameraView(entity, fallbackAspectRatio, transform, camera);
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

