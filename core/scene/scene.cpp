// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "scene.h"

#include <core/ecs/world.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_SCENE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_scene{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr f32 s_DefaultSceneViewYaw = 0.82f;
static constexpr f32 s_DefaultSceneViewPitch = 0.94f;
static constexpr f32 s_DefaultSceneViewDepthOffset = 2.2f;

[[nodiscard]] bool SceneFloat3Finite(const Float4& value){
    const SIMDVector valueVector = LoadFloat(value);
    return !Vector3IsNaN(valueVector) && !Vector3IsInfinite(valueVector);
}

void StoreRotatedSceneViewBasisVector(Float4& outVector, const Float4& localVector, const SIMDVector rotation){
    StoreFloat(Vector3Rotate(LoadFloat(localVector), rotation), &outVector);
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
        static_cast<void>(entity);
        return scene.mainCamera;
    }

    return ECS::ENTITY_ID_INVALID;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SceneViewBasis BuildDefaultSceneViewBasis(){
    SIMDVector sinAngles;
    SIMDVector cosAngles;
    VectorSinCos(
        &sinAngles,
        &cosAngles,
        VectorSet(__hidden_scene::s_DefaultSceneViewYaw, __hidden_scene::s_DefaultSceneViewPitch, 0.0f, 0.0f)
    );
    const f32 sinYaw = VectorGetX(sinAngles);
    const f32 cosYaw = VectorGetX(cosAngles);
    const f32 sinPitch = VectorGetY(sinAngles);
    const f32 cosPitch = VectorGetY(cosAngles);

    SceneViewBasis basis;
    basis.right = Float4(cosYaw, 0.0f, sinYaw, 0.0f);
    basis.up = Float4(sinYaw * sinPitch, cosPitch, -cosYaw * sinPitch, 0.0f);
    basis.forward = Float4(-sinYaw * cosPitch, sinPitch, cosYaw * cosPitch, 0.0f);
    basis.positionDepthBias.w = __hidden_scene::s_DefaultSceneViewDepthOffset;
    return basis;
}

SceneViewBasis BuildSceneViewBasis(const TransformComponent& transform){
    SceneViewBasis basis;
    basis.positionDepthBias = transform.position;
    const SIMDVector rotation = LoadFloat(transform.rotation);
    __hidden_scene::StoreRotatedSceneViewBasisVector(basis.right, Float4(1.0f, 0.0f, 0.0f), rotation);
    __hidden_scene::StoreRotatedSceneViewBasisVector(basis.up, Float4(0.0f, 1.0f, 0.0f), rotation);
    __hidden_scene::StoreRotatedSceneViewBasisVector(basis.forward, Float4(0.0f, 0.0f, 1.0f), rotation);
    return basis;
}

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

    return
        requestedCamera.valid()
            ? requestedCamera
            : fallbackCamera
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_SCENE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

