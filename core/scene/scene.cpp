// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "scene.h"

#include <core/ecs/world.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_SCENE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_scene{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool SceneFloat3Finite(const AlignedFloat3Data& value){
    return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z);
}

[[nodiscard]] bool SceneCameraTransformValid(const TransformComponent& transform){
    constexpr f32 s_CameraRotationUnitLengthSquaredTolerance = 0.001f;
    const f32 rotationLengthSquared =
        transform.rotation.x * transform.rotation.x
        + transform.rotation.y * transform.rotation.y
        + transform.rotation.z * transform.rotation.z
        + transform.rotation.w * transform.rotation.w
    ;

    return SceneFloat3Finite(transform.position)
        && IsFinite(transform.rotation.x)
        && IsFinite(transform.rotation.y)
        && IsFinite(transform.rotation.z)
        && IsFinite(transform.rotation.w)
        && SceneFloat3Finite(transform.scale)
        && IsFinite(rotationLengthSquared)
        && rotationLengthSquared >= 1.0f - s_CameraRotationUnitLengthSquaredTolerance
        && rotationLengthSquared <= 1.0f + s_CameraRotationUnitLengthSquaredTolerance;
}

[[nodiscard]] bool TryBuildSceneCameraView(
    const ECS::EntityID entity,
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

[[nodiscard]] ECS::EntityID ResolveSceneMainCamera(ECS::World& world){
    const auto sceneView = world.view<SceneComponent>();
    for(auto it = sceneView.begin(); it != sceneView.end(); ++it){
        auto&& [entity, scene] = *it;
        (void)entity;
        return scene.mainCamera;
    }

    return ECS::ENTITY_ID_INVALID;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SceneCameraView ResolveSceneCameraView(ECS::World& world, const f32 fallbackAspectRatio){
    const ECS::EntityID mainCamera = __hidden_scene::ResolveSceneMainCamera(world);
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

    return requestedCamera.valid()
        ? requestedCamera
        : fallbackCamera
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_SCENE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

