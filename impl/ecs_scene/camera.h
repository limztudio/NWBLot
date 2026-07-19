// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "components.h"

#include <core/ecs/entity_id.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_SCENE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct alignas(Float4) CameraProjectionData{
    Float4 projectionParams = Float4(0.0f, 0.0f, 0.0f, 0.0f);
    f32 aspectRatio = 1.0f;
    f32 tanHalfVerticalFov = 0.0f;
};

static_assert(IsStandardLayout_V<CameraProjectionData>, "CameraProjectionData must stay layout-stable");
static_assert(IsTriviallyCopyable_V<CameraProjectionData>, "CameraProjectionData must stay cheap to pass by value");
static_assert(alignof(CameraProjectionData) >= alignof(Float4), "CameraProjectionData must keep projection params aligned");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool TryComputeCameraTanHalfVerticalFov(const f32 verticalFovRadians, f32& outTanHalfFov){
    constexpr f32 s_CameraFovCosEpsilon = 0.000001f;

    outTanHalfFov = 0.0f;
    if(
        !IsFinite(verticalFovRadians)
        || verticalFovRadians <= 0.0f
        || verticalFovRadians >= s_PI
    )
        return false;

    SIMDVector sinHalfFovVector;
    SIMDVector cosHalfFovVector;
    VectorSinCos(&sinHalfFovVector, &cosHalfFovVector, VectorReplicate(verticalFovRadians * 0.5f));
    if(
        !VectorIsFinite(sinHalfFovVector, 0xFu)
        || !VectorIsFinite(cosHalfFovVector, 0xFu)
        || !Vector4GreaterOrEqual(
            VectorAbs(cosHalfFovVector),
            VectorReplicate(s_CameraFovCosEpsilon)
        )
    )
        return false;

    const SIMDVector tanHalfFovVector = VectorDivide(sinHalfFovVector, cosHalfFovVector);
    if(!VectorIsFinite(tanHalfFovVector, 0xFu) || !Vector4Greater(tanHalfFovVector, VectorZero()))
        return false;

    outTanHalfFov = VectorGetX(tanHalfFovVector);
    return true;
}

[[nodiscard]] inline bool CameraClipRangeValid(const CameraComponent& camera){
    return IsFinite(camera.nearPlane()) && IsFinite(camera.farPlane()) && camera.nearPlane() > 0.0f && camera.nearPlane() < camera.farPlane();
}

[[nodiscard]] inline f32 ResolveCameraAspectRatio(const CameraComponent& camera, const f32 fallbackAspectRatio){
    if(IsFinite(camera.aspectRatio()) && camera.aspectRatio() > 0.0f)
        return camera.aspectRatio();
    if(IsFinite(fallbackAspectRatio) && fallbackAspectRatio > 0.0f)
        return fallbackAspectRatio;
    return 1.0f;
}

[[nodiscard]] inline bool CameraProjectionDataValid(
    const SIMDVector projectionParams,
    const f32 aspectRatio,
    const f32 tanHalfVerticalFov
){
    return
        IsFinite(aspectRatio)
        && IsFinite(tanHalfVerticalFov)
        && !Vector4IsNaN(projectionParams)
        && !Vector4IsInfinite(projectionParams)
        && aspectRatio > 0.0f
        && tanHalfVerticalFov > 0.0f
        && Vector3Greater(projectionParams, VectorZero())
        && VectorGetW(projectionParams) < 0.0f
    ;
}

[[nodiscard]] inline bool TryBuildCameraProjectionData(
    const CameraComponent& camera,
    const f32 fallbackAspectRatio,
    CameraProjectionData& outProjectionData
){
    outProjectionData = CameraProjectionData{};

    f32 tanHalfFov = 0.0f;
    if(
        !TryComputeCameraTanHalfVerticalFov(camera.verticalFovRadians(), tanHalfFov)
        || !CameraClipRangeValid(camera)
    )
        return false;

    const f32 aspectRatio = ResolveCameraAspectRatio(camera, fallbackAspectRatio);
    const f32 depthRange = camera.farPlane() - camera.nearPlane();
    CameraProjectionData projectionData;
    projectionData.aspectRatio = aspectRatio;
    projectionData.tanHalfVerticalFov = tanHalfFov;
    const SIMDVector projectionDenominators = VectorMultiply(
        VectorSet(tanHalfFov, tanHalfFov, depthRange, depthRange),
        VectorSet(aspectRatio, 1.0f, 1.0f, 1.0f)
    );
    const SIMDVector projectionNumerators = VectorMultiply(
        VectorSet(1.0f, 1.0f, camera.farPlane(), camera.nearPlane()),
        VectorSet(1.0f, 1.0f, 1.0f, -camera.farPlane())
    );
    const SIMDVector projectionParams = VectorDivide(projectionNumerators, projectionDenominators);
    if(!CameraProjectionDataValid(
        projectionParams,
        projectionData.aspectRatio,
        projectionData.tanHalfVerticalFov
    ))
        return false;

    StoreFloat(projectionParams, &projectionData.projectionParams);
    outProjectionData = projectionData;
    return true;
}

[[nodiscard]] inline bool TryBuildCameraProjectionParams(
    const CameraComponent& camera,
    const f32 fallbackAspectRatio,
    Float4& outProjectionParams
){
    CameraProjectionData projectionData;
    if(!TryBuildCameraProjectionData(camera, fallbackAspectRatio, projectionData)){
        outProjectionParams = Float4(0.0f, 0.0f, 0.0f, 0.0f);
        return false;
    }

    outProjectionParams = projectionData.projectionParams;
    return true;
}

[[nodiscard]] inline Float4 BuildDefaultCameraProjectionParams(const f32 fallbackAspectRatio = 1.0f){
    Float4 projectionParams;
    if(TryBuildCameraProjectionParams(CameraComponent{}, fallbackAspectRatio, projectionParams))
        return projectionParams;

    return Float4(1.0f, 1.0f, 1.0f, 0.0f);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SceneCameraView{
    Core::ECS::EntityID entity = Core::ECS::ENTITY_ID_INVALID;
    TransformComponent* transform = nullptr;
    CameraComponent* camera = nullptr;
    CameraProjectionData projectionData;

    [[nodiscard]] bool valid()const noexcept{
        return
            entity.valid()
            && transform != nullptr
            && camera != nullptr
            && CameraProjectionDataValid(
                LoadFloat(projectionData.projectionParams),
                projectionData.aspectRatio,
                projectionData.tanHalfVerticalFov
            )
        ;
    }
};


[[nodiscard]] SceneCameraView ResolveSceneCameraView(Core::ECS::World& world, f32 fallbackAspectRatio = 1.0f);
[[nodiscard]] Core::ECS::EntityID CreateSceneCameraEntity(Core::ECS::World& world, const Float4& position);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_SCENE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

