// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "camera.h"

#include <core/ecs/ecs.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_camera{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool SceneFloat3Finite(const Float4& value){
    const SIMDVector valueVector = LoadFloat(value);
    return !Vector3IsNaN(valueVector) && !Vector3IsInfinite(valueVector);
}

[[nodiscard]] bool SceneCameraTransformValid(const TransformComponent& transform){
    constexpr f32 s_CameraRotationUnitLengthSquaredTolerance = 0.001f;
    const SIMDVector rotation = LoadFloat(transform.rotation);
    const f32 rotationLengthSquared = VectorGetX(QuaternionLengthSq(rotation));

    return
        SceneFloat3Finite(transform.position)
        && !QuaternionIsNaN(rotation)
        && !QuaternionIsInfinite(rotation)
        && SceneFloat3Finite(transform.scale)
        && IsFinite(rotationLengthSquared)
        && rotationLengthSquared >= 1.0f - s_CameraRotationUnitLengthSquaredTolerance
        && rotationLengthSquared <= 1.0f + s_CameraRotationUnitLengthSquaredTolerance
    ;
}

[[nodiscard]] bool TryBuildSceneCameraView(
    const Core::ECS::EntityID entity,
    TransformComponent& transform,
    CameraComponent& camera,
    const f32 fallbackAspectRatio,
    SceneCameraView& outCameraView
){
    outCameraView = SceneCameraView{};
    if(!SceneCameraTransformValid(transform))
        return false;

    CameraProjectionData projectionData;
    if(!TryBuildCameraProjectionData(camera, fallbackAspectRatio, projectionData))
        return false;

    outCameraView.entity = entity;
    outCameraView.transform = &transform;
    outCameraView.camera = &camera;
    outCameraView.projectionData = projectionData;
    return true;
}

[[nodiscard]] Core::ECS::EntityID ResolveActiveCamera(Core::ECS::World& world){
    const auto activeCameraView = world.view<ActiveCameraComponent>();
    for(auto it = activeCameraView.begin(); it != activeCameraView.end(); ++it){
        auto&& [entity, activeCamera] = *it;
        static_cast<void>(entity);
        return activeCamera.camera;
    }

    return Core::ECS::ENTITY_ID_INVALID;
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
    const Core::ECS::EntityID activeCamera = __hidden_ecs_camera::ResolveActiveCamera(world);
    SceneCameraView fallbackCamera;
    SceneCameraView requestedCamera;

    const auto cameraView = world.view<TransformComponent, CameraComponent>();
    for(auto it = cameraView.begin(); it != cameraView.end(); ++it){
        auto&& [entity, transform, camera] = *it;
        SceneCameraView resolvedCamera;
        if(!__hidden_ecs_camera::TryBuildSceneCameraView(entity, transform, camera, fallbackAspectRatio, resolvedCamera))
            continue;

        if(!fallbackCamera.valid())
            fallbackCamera = resolvedCamera;
        if(activeCamera.valid() && entity == activeCamera){
            requestedCamera = resolvedCamera;
            break;
        }
    }

    return
        requestedCamera.valid()
            ? requestedCamera
            : fallbackCamera
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

