// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "components.h"

#include <core/ecs/entity_id.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_SCENE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct alignas(SIMDVector) CameraProjection{
    SIMDVector projectionParams = s_SIMDZero;
    SIMDVector aspectRatio = s_SIMDZero;
    SIMDVector tanHalfVerticalFov = s_SIMDZero;
    SIMDVector nearPlane = s_SIMDZero;
    SIMDVector farPlane = s_SIMDZero;
};

static_assert(IsStandardLayout_V<CameraProjection>, "CameraProjection must stay layout-stable");
static_assert(IsTriviallyCopyable_V<CameraProjection>, "CameraProjection must stay cheap to pass by value");
static_assert(alignof(CameraProjection) >= alignof(SIMDVector), "CameraProjection must keep calculation vectors aligned");


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

[[nodiscard]] inline bool CameraProjectionValid(const CameraProjection& projection){
    const SIMDVector validProjectionParams = VectorOrInt(
        VectorAndInt(VectorGreater(projection.projectionParams, s_SIMDZero), s_SIMDMask3),
        VectorAndInt(VectorLess(projection.projectionParams, s_SIMDZero), s_SIMDMaskW)
    );
    return
        !Vector4IsNaN(projection.projectionParams)
        && !Vector4IsInfinite(projection.projectionParams)
        && !Vector4IsNaN(projection.aspectRatio)
        && !Vector4IsInfinite(projection.aspectRatio)
        && !Vector4IsNaN(projection.tanHalfVerticalFov)
        && !Vector4IsInfinite(projection.tanHalfVerticalFov)
        && Vector4Greater(projection.aspectRatio, s_SIMDZero)
        && Vector4Greater(projection.tanHalfVerticalFov, s_SIMDZero)
        && CameraClipRangeValid(projection.nearPlane, projection.farPlane)
        && VectorMoveMask(validProjectionParams) == VectorComponentMask::s_XYZW
    ;
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

    SIMDVector tanHalfFov;
    if(
        !TryComputeCameraTanHalfVerticalFov(verticalFovRadians, tanHalfFov)
        || !CameraClipRangeValid(nearPlane, farPlane)
    )
        return false;

    const SIMDVector aspectRatio = ResolveCameraAspectRatio(cameraAspectRatio, fallbackAspectRatio);
    const SIMDVector depthRange = VectorSubtract(farPlane, nearPlane);
    const SIMDVector selectZW = VectorOrInt(s_SIMDMaskZ, s_SIMDMaskW);
    CameraProjection projection;
    projection.aspectRatio = aspectRatio;
    projection.tanHalfVerticalFov = tanHalfFov;
    projection.nearPlane = nearPlane;
    projection.farPlane = farPlane;
    const SIMDVector projectionDenominators = VectorMultiply(
        VectorSelect(tanHalfFov, depthRange, selectZW),
        VectorSelect(s_SIMDOne, aspectRatio, s_SIMDMaskX)
    );
    const SIMDVector projectionNumerators = VectorMultiply(
        VectorSelect(s_SIMDOne, VectorMergeXY(farPlane, nearPlane), selectZW),
        VectorSelect(s_SIMDOne, VectorNegate(farPlane), s_SIMDMaskW)
    );
    projection.projectionParams = VectorDivide(projectionNumerators, projectionDenominators);
    if(!CameraProjectionValid(projection))
        return false;

    outProjection = projection;
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

    projection.projectionParams = VectorSet(1.0f, 1.0f, 1.0f, 0.0f);
    projection.aspectRatio = s_SIMDOne;
    projection.tanHalfVerticalFov = s_SIMDOne;
    projection.nearPlane = VectorReplicate(CameraDefaults::s_NearPlane);
    projection.farPlane = VectorReplicate(CameraDefaults::s_FarPlane);
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
            && CameraProjectionValid(projection)
        ;
    }
};


[[nodiscard]] SceneCameraView ResolveSceneCameraView(Core::ECS::World& world, f32 fallbackAspectRatio = CameraDefaults::s_FallbackAspectRatio);
[[nodiscard]] Core::ECS::EntityID CreateSceneCameraEntity(Core::ECS::World& world, const Float4& position);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_SCENE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

