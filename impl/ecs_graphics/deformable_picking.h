// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "deformable_runtime_mesh_cache.h"

#include <core/alloc/scratch.h>
#include <core/scene/transform_component.h>

#include <cstddef>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererSystem;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct alignas(AlignedFloat4Data) DeformablePickingRay{
    // xyz = origin, w = minimum hit distance.
    AlignedFloat4Data originMinDistance = AlignedFloat4Data(0.0f, 0.0f, 0.0f, 0.0f);
    // xyz = direction, w = maximum hit distance.
    AlignedFloat4Data directionMaxDistance = AlignedFloat4Data(0.0f, 0.0f, 1.0f, Limit<f32>::s_Max);

    [[nodiscard]] Float3Data origin()const{
        return Float3Data(originMinDistance.x, originMinDistance.y, originMinDistance.z);
    }

    [[nodiscard]] Float3Data direction()const{
        return Float3Data(directionMaxDistance.x, directionMaxDistance.y, directionMaxDistance.z);
    }

    [[nodiscard]] f32 minDistance()const{ return originMinDistance.w; }
    [[nodiscard]] f32 maxDistance()const{ return directionMaxDistance.w; }

    void setOrigin(const Float3Data& value){
        originMinDistance.x = value.x;
        originMinDistance.y = value.y;
        originMinDistance.z = value.z;
    }

    void setDirection(const Float3Data& value){
        directionMaxDistance.x = value.x;
        directionMaxDistance.y = value.y;
        directionMaxDistance.z = value.z;
    }

    void setMinDistance(const f32 value){ originMinDistance.w = value; }
    void setMaxDistance(const f32 value){ directionMaxDistance.w = value; }
};
static_assert(IsStandardLayout_V<DeformablePickingRay>, "DeformablePickingRay must stay layout-stable");
static_assert(IsTriviallyCopyable_V<DeformablePickingRay>, "DeformablePickingRay must stay cheap to pass by value");
static_assert(sizeof(DeformablePickingRay) == sizeof(AlignedFloat4Data) * 2u, "DeformablePickingRay layout drifted");
static_assert(alignof(DeformablePickingRay) >= alignof(AlignedFloat4Data), "DeformablePickingRay must stay SIMD-aligned");

struct DeformablePickingInputs{
    const DeformableMorphWeightsComponent* morphWeights = nullptr;
    const DeformableJointPaletteComponent* jointPalette = nullptr;
    const DeformableDisplacementComponent* displacement = nullptr;
    const Core::Scene::TransformComponent* transform = nullptr;
};

struct alignas(AlignedFloat4Data) DeformableHitBarycentric{
    // xyz = barycentric coordinates, w = ray hit distance.
    AlignedFloat4Data values = AlignedFloat4Data(0.0f, 0.0f, 0.0f, 0.0f);

    [[nodiscard]] f32& operator[](const usize index){
        NWB_ASSERT(index < 3u);
        return (&values.x)[index];
    }

    [[nodiscard]] const f32& operator[](const usize index)const{
        NWB_ASSERT(index < 3u);
        return (&values.x)[index];
    }

    [[nodiscard]] f32 distance()const{ return values.w; }
    void setDistance(const f32 value){ values.w = value; }
};
static_assert(IsStandardLayout_V<DeformableHitBarycentric>, "DeformableHitBarycentric must stay layout-stable");
static_assert(IsTriviallyCopyable_V<DeformableHitBarycentric>, "DeformableHitBarycentric must stay cheap to copy");
static_assert(
    sizeof(DeformableHitBarycentric) == sizeof(AlignedFloat4Data),
    "DeformableHitBarycentric must stay one aligned vector wide"
);
static_assert(
    alignof(DeformableHitBarycentric) >= alignof(AlignedFloat4Data),
    "DeformableHitBarycentric must stay SIMD-aligned"
);

struct alignas(AlignedFloat4Data) DeformablePosedHit{
    Core::ECS::EntityID entity = Core::ECS::ENTITY_ID_INVALID;
    RuntimeMeshHandle runtimeMesh;
    u32 editRevision = 0;
    u32 triangle = 0;
    DeformableHitBarycentric bary;
    AlignedFloat4Data position = AlignedFloat4Data(0.0f, 0.0f, 0.0f, 1.0f);
    AlignedFloat4Data normal = AlignedFloat4Data(0.0f, 0.0f, 1.0f, 0.0f);
    SourceSample restSample;

    [[nodiscard]] f32 distance()const{ return bary.distance(); }
    void setDistance(const f32 value){ bary.setDistance(value); }

    void setPosition(const Float3Data& value){
        position = AlignedFloat4Data(value.x, value.y, value.z, 1.0f);
    }

    void setNormal(const Float3Data& value){
        normal = AlignedFloat4Data(value.x, value.y, value.z, 0.0f);
    }
};
static_assert(IsStandardLayout_V<DeformablePosedHit>, "DeformablePosedHit must stay layout-stable");
static_assert(IsTriviallyCopyable_V<DeformablePosedHit>, "DeformablePosedHit must stay cheap to copy");
static_assert(alignof(DeformablePosedHit) >= alignof(AlignedFloat4Data), "DeformablePosedHit must stay SIMD-aligned");
static_assert(
    (offsetof(DeformablePosedHit, bary) % alignof(AlignedFloat4Data)) == 0,
    "DeformablePosedHit::bary must stay SIMD-aligned"
);
static_assert(
    (offsetof(DeformablePosedHit, position) % alignof(AlignedFloat4Data)) == 0,
    "DeformablePosedHit::position must stay SIMD-aligned"
);
static_assert(
    (offsetof(DeformablePosedHit, normal) % alignof(AlignedFloat4Data)) == 0,
    "DeformablePosedHit::normal must stay SIMD-aligned"
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool BuildDeformablePickingVertices(
    const DeformableRuntimeMeshInstance& instance,
    const DeformablePickingInputs& inputs,
    Vector<DeformableVertexRest>& outVertices
);
[[nodiscard]] bool BuildDeformablePickingVertices(
    const DeformableRuntimeMeshInstance& instance,
    const DeformablePickingInputs& inputs,
    Vector<DeformableVertexRest, Core::Alloc::ScratchAllocator<DeformableVertexRest>>& outVertices
);
[[nodiscard]] bool ResolveDeformableRestSurfaceSample(
    const DeformableRuntimeMeshInstance& instance,
    u32 triangle,
    const f32 (&bary)[3],
    SourceSample& outSample
);
[[nodiscard]] bool ResolveDeformableRestSurfaceSample(
    const DeformableRuntimeMeshInstance& instance,
    u32 triangle,
    const DeformableHitBarycentric& bary,
    SourceSample& outSample
);
[[nodiscard]] bool RaycastDeformableRuntimeMesh(
    const DeformableRuntimeMeshInstance& instance,
    const DeformablePickingInputs& inputs,
    const DeformablePickingRay& ray,
    DeformablePosedHit& outHit
);
[[nodiscard]] bool RaycastVisibleDeformableRenderers(
    Core::ECS::World& world,
    const RendererSystem& rendererSystem,
    const DeformablePickingRay& ray,
    DeformablePosedHit& outHit
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

