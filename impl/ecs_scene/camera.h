// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_scene/camera_types.adhd.h>
#include <impl/ecs_scene/components.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_SCENE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static_assert(IsStandardLayout_V<ActiveCameraComponent>, "ActiveCameraComponent must stay layout-stable for ECS storage");
static_assert(IsTriviallyCopyable_V<ActiveCameraComponent>, "ActiveCameraComponent must stay cheap to move in dense ECS storage");
static_assert(sizeof(ActiveCameraComponent) == sizeof(Core::ECS::EntityID), "ActiveCameraComponent must only contain the active camera entity reference");

static_assert(IsStandardLayout_V<CameraComponent>, "CameraComponent must stay layout-stable for ECS storage");
static_assert(IsTriviallyCopyable_V<CameraComponent>, "CameraComponent must stay cheap to move in dense ECS storage");
static_assert(alignof(CameraComponent) >= alignof(Float4), "CameraComponent must stay aligned for SIMD component loads");
static_assert(sizeof(CameraComponent) == sizeof(Float4), "CameraComponent must stay one aligned vector wide");
static_assert((sizeof(CameraComponent) % alignof(CameraComponent)) == 0u, "CameraComponent array stride must keep every element SIMD-aligned");
static_assert((offsetof(CameraComponent, projection) % alignof(Float4)) == 0u, "CameraComponent::projection must stay aligned");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static_assert(IsStandardLayout_V<CameraProjection>, "CameraProjection must stay layout-stable");
static_assert(IsTriviallyCopyable_V<CameraProjection>, "CameraProjection must stay cheap to pass by value");
static_assert(alignof(CameraProjection) >= alignof(Float4), "CameraProjection must keep storage vectors aligned");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<SIMDVectorSingleResult> TryComputeCameraTanHalfVerticalFov(SIMDVectorSingle verticalFovRadians)noexcept;

[[nodiscard]] inline b8 CameraClipRangeValid(const SIMDVectorSingle nearPlane, const SIMDVectorSingle farPlane)noexcept{
    return VectorIsFinite(nearPlane, VectorComponentMask::s_XYZW) && VectorIsFinite(farPlane, VectorComponentMask::s_XYZW) && Vector4Greater(nearPlane, s_SIMDZero) && Vector4Less(nearPlane, farPlane);
}

[[nodiscard]] SIMDVectorSingle ResolveCameraAspectRatio(SIMDVectorSingle cameraAspectRatio, SIMDVectorSingle fallbackAspectRatio)noexcept;
[[nodiscard]] b8 CameraProjectionValuesValid(SIMDVectorSingle projectionParams, SIMDVectorSingle aspectRatio, SIMDVectorSingle tanHalfVerticalFov, SIMDVectorSingle nearPlane, SIMDVectorSingle farPlane)noexcept;
[[nodiscard]] Expected<SIMDMatrix> TryBuildCameraProjection(
    SIMDVectorSingle verticalFovRadians,
    SIMDVectorSingle nearPlane,
    SIMDVectorSingle farPlane,
    SIMDVectorSingle cameraAspectRatio,
    SIMDVectorSingle fallbackAspectRatio
)noexcept;

// Explicit boundary for retaining a projection; calculation helpers return SIMD rows.
[[nodiscard]] CameraProjection StoreCameraProjection(const SIMDMatrix& values)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SceneCameraView{
    Core::ECS::EntityID entity = Core::ECS::s_InvalidEntityId;
    TransformComponent* transform = nullptr;
    CameraComponent* camera = nullptr;
    CameraProjection projection;


public:
    [[nodiscard]] b8 valid()const noexcept;
};

[[nodiscard]] SceneCameraView ResolveSceneCameraView(Core::ECS::World& world, f32 fallbackAspectRatio = s_CameraFallbackAspectRatio)noexcept;
[[nodiscard]] Core::ECS::EntityID CreateSceneCameraEntity(Core::ECS::World& world, const Float4& position);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_SCENE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

