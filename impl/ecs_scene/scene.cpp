// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "scene.h"

#include <core/ecs/world.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_scene{


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

[[nodiscard]] Core::ECS::EntityID ResolveSceneMainCamera(Core::ECS::World& world){
    const auto sceneView = world.view<SceneComponent>();
    for(auto it = sceneView.begin(); it != sceneView.end(); ++it){
        auto&& [entity, scene] = *it;
        static_cast<void>(entity);
        return scene.mainCamera;
    }

    return Core::ECS::ENTITY_ID_INVALID;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SceneCameraView ResolveSceneCameraView(Core::ECS::World& world, const f32 fallbackAspectRatio){
    const Core::ECS::EntityID mainCamera = __hidden_scene::ResolveSceneMainCamera(world);
    SceneCameraView fallbackCamera;
    SceneCameraView requestedCamera;

    const auto cameraView = world.view<TransformComponent, CameraComponent>();
    for(auto it = cameraView.begin(); it != cameraView.end(); ++it){
        auto&& [entity, transform, camera] = *it;
        SceneCameraView resolvedCamera;
        if(!__hidden_scene::TryBuildSceneCameraView(entity, transform, camera, fallbackAspectRatio, resolvedCamera))
            continue;

        if(!fallbackCamera.valid())
            fallbackCamera = resolvedCamera;
        if(mainCamera.valid() && entity == mainCamera){
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

