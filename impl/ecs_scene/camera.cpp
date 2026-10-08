// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "camera.h"

#include <core/ecs/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_SCENE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_camera{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool SceneCameraTransformValid(
    const SIMDVector position,
    const SIMDVector rotation,
    const SIMDVector scale
)noexcept{
    constexpr f32 s_CameraRotationUnitLengthSquaredTolerance = 0.001f;
    const f32 rotationLengthSquared = VectorGetX(QuaternionLengthSq(rotation));

    return
        Vector3IsFinite(position)
        && !QuaternionIsNaN(rotation)
        && !QuaternionIsInfinite(rotation)
        && Vector3IsFinite(scale)
        && IsFinite(rotationLengthSquared)
        && rotationLengthSquared >= 1.0f - s_CameraRotationUnitLengthSquaredTolerance
        && rotationLengthSquared <= 1.0f + s_CameraRotationUnitLengthSquaredTolerance
    ;
}

[[nodiscard]] Expected<SceneCameraView> TryBuildSceneCameraView(
    const Core::ECS::EntityID entity,
    TransformComponent& transform,
    CameraComponent& camera,
    const f32 fallbackAspectRatio,
    const SIMDVector position,
    const SIMDVector rotation,
    const SIMDVector scale
)noexcept{
    if(!SceneCameraTransformValid(position, rotation, scale))
        return MakeUnexpected(Failure{});

    const auto projection = TryBuildCameraProjection(
        VectorReplicate(camera.verticalFovRadians()),
        VectorReplicate(camera.nearPlane()),
        VectorReplicate(camera.farPlane()),
        VectorReplicate(camera.aspectRatio()),
        VectorReplicate(fallbackAspectRatio)
    );
    if(!projection)
        return MakeUnexpected(Failure{});

    return SceneCameraView{ entity, &transform, &camera, *projection };
}

[[nodiscard]] Core::ECS::EntityID ResolveActiveCamera(Core::ECS::World& world){
    const auto activeCameraView = world.view<ActiveCameraComponent>();
    for(auto it = activeCameraView.begin(); it != activeCameraView.end(); ++it){
        auto&& [entity, activeCamera] = *it;
        static_cast<void>(entity);
        return activeCamera.camera;
    }

    return Core::ECS::s_InvalidEntityId;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Core::ECS::EntityID CreateSceneCameraEntity(Core::ECS::World& world, const Float4& position){
    auto cameraEntity = world.createEntity();
    auto& transform = cameraEntity.addComponent<TransformComponent>();
    transform.position = position;
    cameraEntity.addComponent<CameraComponent>();
    return cameraEntity.id();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SceneCameraView ResolveSceneCameraView(Core::ECS::World& world, const f32 fallbackAspectRatio){
    const Core::ECS::EntityID activeCamera = __hidden_camera::ResolveActiveCamera(world);
    if(activeCamera.valid()){
        auto* transform = world.tryGetComponent<TransformComponent>(activeCamera);
        auto* camera = world.tryGetComponent<CameraComponent>(activeCamera);
        if(transform && camera){
            const auto requestedCamera = __hidden_camera::TryBuildSceneCameraView(
                activeCamera,
                *transform,
                *camera,
                fallbackAspectRatio,
                LoadFloat(transform->position),
                LoadFloat(transform->rotation),
                LoadFloat(transform->scale)
            );
            if(requestedCamera)
                return *requestedCamera;
        }
    }

    const auto cameraView = world.view<TransformComponent, CameraComponent>();
    for(auto it = cameraView.begin(); it != cameraView.end(); ++it){
        auto&& [entity, transform, camera] = *it;
        const auto resolvedCamera = __hidden_camera::TryBuildSceneCameraView(
            entity,
            transform,
            camera,
            fallbackAspectRatio,
            LoadFloat(transform.position),
            LoadFloat(transform.rotation),
            LoadFloat(transform.scale)
        );
        if(resolvedCamera)
            return *resolvedCamera;
    }

    return {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_SCENE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

