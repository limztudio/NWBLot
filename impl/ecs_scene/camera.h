// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "components.h"

#include <core/ecs/entity_id.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_SCENE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct alignas(Float4) CameraProjection{
    // Persistent projection payload. Keep SIMD values in calculation helpers and convert only at this boundary.
    Float4 projectionParams = Float4(0.0f, 0.0f, 0.0f, 0.0f);
    f32 aspectRatio = 0.0f;
    f32 tanHalfVerticalFov = 0.0f;
    f32 nearPlane = 0.0f;
    f32 farPlane = 0.0f;
};

static_assert(IsStandardLayout_V<CameraProjection>, "CameraProjection must stay layout-stable");
static_assert(IsTriviallyCopyable_V<CameraProjection>, "CameraProjection must stay cheap to pass by value");
static_assert(alignof(CameraProjection) >= alignof(Float4), "CameraProjection must keep storage vectors aligned");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool TryComputeCameraTanHalfVerticalFov(const SIMDVector verticalFovRadians, SIMDVector& outTanHalfFov){
    constexpr f32 s_CameraFovCosEpsilon = 0.000001f;

    outTanHalfFov = s_SIMDZero;
    if(
        !VectorIsFinite(verticalFovRadians, VectorComponentMask::s_XYZW)
        || !Vector4Greater(verticalFovRadians, s_SIMDZero)
        || !Vector4Less(verticalFovRadians, VectorReplicate(s_PI))
    )
        return false;

    SIMDVector sinHalfFovVector;
    SIMDVector cosHalfFovVector;
    VectorSinCos(&sinHalfFovVector, &cosHalfFovVector, VectorScale(verticalFovRadians, 0.5f));
    if(
        !VectorIsFinite(sinHalfFovVector, VectorComponentMask::s_XYZW)
        || !VectorIsFinite(cosHalfFovVector, VectorComponentMask::s_XYZW)
        || !Vector4GreaterOrEqual(
            VectorAbs(cosHalfFovVector),
            VectorReplicate(s_CameraFovCosEpsilon)
        )
    )
        return false;

    const SIMDVector tanHalfFovVector = VectorDivide(sinHalfFovVector, cosHalfFovVector);
    if(!VectorIsFinite(tanHalfFovVector, VectorComponentMask::s_XYZW) || !Vector4Greater(tanHalfFovVector, VectorZero()))
        return false;

    outTanHalfFov = tanHalfFovVector;
    return true;
}

[[nodiscard]] inline bool CameraClipRangeValid(const SIMDVector nearPlane, const SIMDVector farPlane){
    return
        VectorIsFinite(nearPlane, VectorComponentMask::s_XYZW)
        && VectorIsFinite(farPlane, VectorComponentMask::s_XYZW)
        && Vector4Greater(nearPlane, s_SIMDZero)
        && Vector4Less(nearPlane, farPlane)
    ;
}

[[nodiscard]] inline SIMDVector ResolveCameraAspectRatio(const SIMDVector cameraAspectRatio, const SIMDVector fallbackAspectRatio){
    const SIMDVector fallbackValid = VectorAndCInt(
        VectorGreater(fallbackAspectRatio, s_SIMDZero),
        VectorOrInt(VectorIsNaN(fallbackAspectRatio), VectorIsInfinite(fallbackAspectRatio))
    );
    const SIMDVector resolvedFallbackAspectRatio = VectorSelect(
        s_SIMDOne,
        fallbackAspectRatio,
        fallbackValid
    );
    const SIMDVector cameraValid = VectorAndCInt(
        VectorGreater(cameraAspectRatio, s_SIMDZero),
        VectorOrInt(VectorIsNaN(cameraAspectRatio), VectorIsInfinite(cameraAspectRatio))
    );
    return VectorSelect(resolvedFallbackAspectRatio, cameraAspectRatio, cameraValid);
}

[[nodiscard]] inline bool CameraProjectionValuesValid(
    const SIMDVector projectionParams,
    const SIMDVector aspectRatio,
    const SIMDVector tanHalfVerticalFov,
    const SIMDVector nearPlane,
    const SIMDVector farPlane
){
    const SIMDVector validProjectionParams = VectorOrInt(
        VectorAndInt(VectorGreater(projectionParams, s_SIMDZero), s_SIMDMask3),
        VectorAndInt(VectorLess(projectionParams, s_SIMDZero), s_SIMDMaskW)
    );
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

[[nodiscard]] inline bool CameraProjectionStorageValid(const CameraProjection& projection){
    return CameraProjectionValuesValid(
        LoadFloat(projection.projectionParams),
        VectorReplicate(projection.aspectRatio),
        VectorReplicate(projection.tanHalfVerticalFov),
        VectorReplicate(projection.nearPlane),
        VectorReplicate(projection.farPlane)
    );
}

[[nodiscard]] inline bool TryBuildCameraProjectionValues(
    const SIMDVector verticalFovRadians,
    const SIMDVector nearPlane,
    const SIMDVector farPlane,
    const SIMDVector cameraAspectRatio,
    const SIMDVector fallbackAspectRatio,
    SIMDVector& outProjectionParams,
    SIMDVector& outAspectRatio,
    SIMDVector& outTanHalfVerticalFov,
    SIMDVector& outNearPlane,
    SIMDVector& outFarPlane
){
    outProjectionParams = s_SIMDZero;
    outAspectRatio = s_SIMDZero;
    outTanHalfVerticalFov = s_SIMDZero;
    outNearPlane = s_SIMDZero;
    outFarPlane = s_SIMDZero;

    SIMDVector tanHalfFov;
    if(
        !TryComputeCameraTanHalfVerticalFov(verticalFovRadians, tanHalfFov)
        || !CameraClipRangeValid(nearPlane, farPlane)
    )
        return false;

    const SIMDVector aspectRatio = ResolveCameraAspectRatio(cameraAspectRatio, fallbackAspectRatio);
    const SIMDVector depthRange = VectorSubtract(farPlane, nearPlane);
    const SIMDVector selectZW = VectorOrInt(s_SIMDMaskZ, s_SIMDMaskW);
    const SIMDVector projectionDenominators = VectorMultiply(
        VectorSelect(tanHalfFov, depthRange, selectZW),
        VectorSelect(s_SIMDOne, aspectRatio, s_SIMDMaskX)
    );
    const SIMDVector projectionNumerators = VectorMultiply(
        VectorSelect(s_SIMDOne, VectorMergeXY(farPlane, nearPlane), selectZW),
        VectorSelect(s_SIMDOne, VectorNegate(farPlane), s_SIMDMaskW)
    );
    const SIMDVector projectionParams = VectorDivide(projectionNumerators, projectionDenominators);
    if(!CameraProjectionValuesValid(projectionParams, aspectRatio, tanHalfFov, nearPlane, farPlane))
        return false;

    outProjectionParams = projectionParams;
    outAspectRatio = aspectRatio;
    outTanHalfVerticalFov = tanHalfFov;
    outNearPlane = nearPlane;
    outFarPlane = farPlane;
    return true;
}

[[nodiscard]] inline bool TryBuildCameraProjection(
    const SIMDVector verticalFovRadians,
    const SIMDVector nearPlane,
    const SIMDVector farPlane,
    const SIMDVector cameraAspectRatio,
    const SIMDVector fallbackAspectRatio,
    CameraProjection& outProjection
){
    outProjection = CameraProjection{};

    SIMDVector projectionParams;
    SIMDVector aspectRatio;
    SIMDVector tanHalfVerticalFov;
    SIMDVector resolvedNearPlane;
    SIMDVector resolvedFarPlane;
    if(!TryBuildCameraProjectionValues(
        verticalFovRadians,
        nearPlane,
        farPlane,
        cameraAspectRatio,
        fallbackAspectRatio,
        projectionParams,
        aspectRatio,
        tanHalfVerticalFov,
        resolvedNearPlane,
        resolvedFarPlane
    ))
        return false;

    StoreFloat(projectionParams, &outProjection.projectionParams);
    outProjection.aspectRatio = VectorGetX(aspectRatio);
    outProjection.tanHalfVerticalFov = VectorGetX(tanHalfVerticalFov);
    outProjection.nearPlane = VectorGetX(resolvedNearPlane);
    outProjection.farPlane = VectorGetX(resolvedFarPlane);
    return true;
}

[[nodiscard]] inline CameraProjection BuildDefaultCameraProjection(const f32 fallbackAspectRatio = CameraDefaults::s_FallbackAspectRatio){
    CameraProjection projection;
    if(TryBuildCameraProjection(
        VectorReplicate(CameraDefaults::s_VerticalFovRadians),
        VectorReplicate(CameraDefaults::s_NearPlane),
        VectorReplicate(CameraDefaults::s_FarPlane),
        VectorReplicate(CameraDefaults::s_AutoAspectRatio),
        VectorReplicate(fallbackAspectRatio),
        projection
    ))
        return projection;

    projection.projectionParams = Float4(1.0f, 1.0f, 1.0f, 0.0f);
    projection.aspectRatio = 1.0f;
    projection.tanHalfVerticalFov = 1.0f;
    projection.nearPlane = CameraDefaults::s_NearPlane;
    projection.farPlane = CameraDefaults::s_FarPlane;
    return projection;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SceneCameraView{
    Core::ECS::EntityID entity = Core::ECS::ENTITY_ID_INVALID;
    TransformComponent* transform = nullptr;
    CameraComponent* camera = nullptr;
    CameraProjection projection;

    [[nodiscard]] bool valid()const noexcept{
        return
            entity.valid()
            && transform != nullptr
            && camera != nullptr
            && CameraProjectionStorageValid(projection)
        ;
    }
};


[[nodiscard]] SceneCameraView ResolveSceneCameraView(Core::ECS::World& world, f32 fallbackAspectRatio = CameraDefaults::s_FallbackAspectRatio);
[[nodiscard]] Core::ECS::EntityID CreateSceneCameraEntity(Core::ECS::World& world, const Float4& position);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_SCENE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

