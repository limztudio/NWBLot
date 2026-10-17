// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "components.h"

#include <core/ecs/entity_id.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_SCENE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct alignas(Float4) CameraProjection{
    // Persistent projection payload; helpers convert at this boundary.
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


[[nodiscard]] inline Expected<SIMDVectorSingle> TryComputeCameraTanHalfVerticalFov(const SIMDVectorSingle verticalFovRadians)noexcept{
    constexpr f32 s_CameraFovCosEpsilon = 0.000001f;

    if(!VectorIsFinite(verticalFovRadians, VectorComponentMask::s_XYZW) || !Vector4Greater(verticalFovRadians, s_SIMDZero) || !Vector4Less(verticalFovRadians, VectorReplicate(s_PI)))
        return MakeUnexpected(Failure{});

    SIMDVectorSingle sinHalfFovVector;
    SIMDVectorSingle cosHalfFovVector;
    constexpr f32 s_HalfFovWeight = 0.5f;
    VectorSinCos(sinHalfFovVector, cosHalfFovVector, VectorScale(verticalFovRadians, s_HalfFovWeight));
    if(!VectorIsFinite(sinHalfFovVector, VectorComponentMask::s_XYZW) || !VectorIsFinite(cosHalfFovVector, VectorComponentMask::s_XYZW) || !Vector4GreaterOrEqual(VectorAbs(cosHalfFovVector), VectorReplicate(s_CameraFovCosEpsilon)))
        return MakeUnexpected(Failure{});

    const SIMDVectorSingle tanHalfFovVector = VectorDivide(sinHalfFovVector, cosHalfFovVector);
    if(!VectorIsFinite(tanHalfFovVector, VectorComponentMask::s_XYZW) || !Vector4Greater(tanHalfFovVector, VectorZero()))
        return MakeUnexpected(Failure{});

    return tanHalfFovVector;
}

[[nodiscard]] inline bool CameraClipRangeValid(const SIMDVectorSingle nearPlane, const SIMDVectorSingle farPlane)noexcept{
    return VectorIsFinite(nearPlane, VectorComponentMask::s_XYZW) && VectorIsFinite(farPlane, VectorComponentMask::s_XYZW) && Vector4Greater(nearPlane, s_SIMDZero) && Vector4Less(nearPlane, farPlane);
}

[[nodiscard]] inline SIMDVectorSingle ResolveCameraAspectRatio(const SIMDVectorSingle cameraAspectRatio, const SIMDVectorSingle fallbackAspectRatio)noexcept{
    const SIMDVectorSingle fallbackValid = VectorAndCInt(VectorGreater(fallbackAspectRatio, s_SIMDZero), VectorOrInt(VectorIsNaN(fallbackAspectRatio), VectorIsInfinite(fallbackAspectRatio)));
    const SIMDVectorSingle resolvedFallbackAspectRatio = VectorSelect(s_SIMDOne, fallbackAspectRatio, fallbackValid);
    const SIMDVectorSingle cameraValid = VectorAndCInt(VectorGreater(cameraAspectRatio, s_SIMDZero), VectorOrInt(VectorIsNaN(cameraAspectRatio), VectorIsInfinite(cameraAspectRatio)));
    return VectorSelect(resolvedFallbackAspectRatio, cameraAspectRatio, cameraValid);
}

[[nodiscard]] inline bool CameraProjectionValuesValid(const SIMDVectorSingle projectionParams, const SIMDVectorSingle aspectRatio, const SIMDVectorSingle tanHalfVerticalFov, const SIMDVectorSingle nearPlane, const SIMDVectorSingle farPlane)noexcept{
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

inline void LoadCameraProjectionValues(const CameraProjection& projection, SIMDVectorSingle& outProjectionParams, SIMDVectorSingle& outAspectRatio, SIMDVectorSingle& outTanHalfVerticalFov, SIMDVectorSingle& outNearPlane, SIMDVectorSingle& outFarPlane)noexcept{
    outProjectionParams = LoadFloat(projection.projectionParams);
    outAspectRatio = VectorReplicate(projection.aspectRatio);
    outTanHalfVerticalFov = VectorReplicate(projection.tanHalfVerticalFov);
    outNearPlane = VectorReplicate(projection.nearPlane);
    outFarPlane = VectorReplicate(projection.farPlane);
}

[[nodiscard]] inline bool CameraProjectionStorageValid(const CameraProjection& projection)noexcept{
    SIMDVectorSingle projectionParams{};
    SIMDVectorSingle aspectRatio{};
    SIMDVectorSingle tanHalfVerticalFov{};
    SIMDVectorSingle nearPlane{};
    SIMDVectorSingle farPlane{};
    LoadCameraProjectionValues(projection, projectionParams, aspectRatio, tanHalfVerticalFov, nearPlane, farPlane);
    return CameraProjectionValuesValid(projectionParams, aspectRatio, tanHalfVerticalFov, nearPlane, farPlane);
}

[[nodiscard]] inline Expected<SIMDMatrix> TryBuildCameraProjection(
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
    const SIMDVectorSingle projectionDenominators = VectorMultiply(VectorSelect(*tanHalfFov, depthRange, selectZW), VectorSelect(s_SIMDOne, aspectRatio, s_SIMDMaskX));
    const SIMDVectorSingle projectionNumerators = VectorMultiply(VectorSelect(s_SIMDOne, VectorMergeXY(farPlane, nearPlane), selectZW), VectorSelect(s_SIMDOne, VectorNegate(farPlane), s_SIMDMaskW));
    const SIMDVectorSingle projectionParams = VectorDivide(projectionNumerators, projectionDenominators);
    if(!CameraProjectionValuesValid(projectionParams, aspectRatio, *tanHalfFov, nearPlane, farPlane))
        return MakeUnexpected(Failure{});

    return SIMDMatrix{ .v = { projectionParams, VectorMergeX(aspectRatio, *tanHalfFov, nearPlane, farPlane), VectorZero(), VectorZero() } };
}

// Explicit boundary for retaining a projection; calculation helpers return SIMD rows.
[[nodiscard]] inline CameraProjection StoreCameraProjection(const SIMDMatrix& values)noexcept{
    CameraProjection projection;
    StoreFloat(values.v[0], projection.projectionParams);
    projection.aspectRatio = VectorGetX(values.v[1]);
    projection.tanHalfVerticalFov = VectorGetY(values.v[1]);
    projection.nearPlane = VectorGetZ(values.v[1]);
    projection.farPlane = VectorGetW(values.v[1]);
    return projection;
}

[[nodiscard]] inline CameraProjection StoreDefaultCameraProjection(const f32 fallbackAspectRatio = CameraDefaults::s_FallbackAspectRatio)noexcept{
    const auto defaultProjection = TryBuildCameraProjection(
        VectorReplicate(CameraDefaults::s_VerticalFovRadians),
        VectorReplicate(CameraDefaults::s_NearPlane),
        VectorReplicate(CameraDefaults::s_FarPlane),
        VectorReplicate(CameraDefaults::s_AutoAspectRatio),
        VectorReplicate(fallbackAspectRatio)
    );
    if(defaultProjection)
        return StoreCameraProjection(*defaultProjection);

    CameraProjection projection;
    projection.projectionParams = Float4(1.0f, 1.0f, 1.0f, 0.0f);
    projection.aspectRatio = 1.0f;
    projection.tanHalfVerticalFov = 1.0f;
    projection.nearPlane = CameraDefaults::s_NearPlane;
    projection.farPlane = CameraDefaults::s_FarPlane;
    return projection;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SceneCameraView{
    Core::ECS::EntityID entity = Core::ECS::s_InvalidEntityId;
    TransformComponent* transform = nullptr;
    CameraComponent* camera = nullptr;
    CameraProjection projection;


public:
    [[nodiscard]] bool valid()const noexcept{
        return entity.valid() && transform != nullptr && camera != nullptr && CameraProjectionStorageValid(projection);
    }
};


[[nodiscard]] SceneCameraView ResolveSceneCameraView(Core::ECS::World& world, f32 fallbackAspectRatio = CameraDefaults::s_FallbackAspectRatio)noexcept;
[[nodiscard]] Core::ECS::EntityID CreateSceneCameraEntity(Core::ECS::World& world, const Float4& position);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_SCENE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

