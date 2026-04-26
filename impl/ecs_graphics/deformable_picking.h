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


struct alignas(Float4) DeformablePickingRay{
    // xyz = origin, w = minimum hit distance.
    Float4 originMinDistance = Float4(0.0f, 0.0f, 0.0f, 0.0f);
    // xyz = direction, w = maximum hit distance.
    Float4 directionMaxDistance = Float4(0.0f, 0.0f, 1.0f, Limit<f32>::s_Max);

    [[nodiscard]] Float3U origin()const{
        return Float3U(originMinDistance.x, originMinDistance.y, originMinDistance.z);
    }

    [[nodiscard]] Float3U direction()const{
        return Float3U(directionMaxDistance.x, directionMaxDistance.y, directionMaxDistance.z);
    }

    [[nodiscard]] f32 minDistance()const{ return originMinDistance.w; }
    [[nodiscard]] f32 maxDistance()const{ return directionMaxDistance.w; }

    void setOrigin(const Float3U& value){
        originMinDistance.x = value.x;
        originMinDistance.y = value.y;
        originMinDistance.z = value.z;
    }

    void setDirection(const Float3U& value){
        directionMaxDistance.x = value.x;
        directionMaxDistance.y = value.y;
        directionMaxDistance.z = value.z;
    }

    void setMinDistance(const f32 value){ originMinDistance.w = value; }
    void setMaxDistance(const f32 value){ directionMaxDistance.w = value; }
};
static_assert(IsStandardLayout_V<DeformablePickingRay>, "DeformablePickingRay must stay layout-stable");
static_assert(IsTriviallyCopyable_V<DeformablePickingRay>, "DeformablePickingRay must stay cheap to pass by value");
static_assert(sizeof(DeformablePickingRay) == sizeof(Float4) * 2u, "DeformablePickingRay layout drifted");
static_assert(alignof(DeformablePickingRay) >= alignof(Float4), "DeformablePickingRay must stay SIMD-aligned");

struct DeformablePickingInputs{
    Core::Assets::AssetManager* assetManager = nullptr;
    const DeformableMorphWeightsComponent* morphWeights = nullptr;
    const DeformableJointPaletteComponent* jointPalette = nullptr;
    const DeformableDisplacementComponent* displacement = nullptr;
    const DeformableDisplacementTexture* displacementTexture = nullptr;
    const Core::Scene::TransformComponent* transform = nullptr;
};

struct alignas(Float4) DeformableHitBarycentric{
    // xyz = barycentric coordinates, w = ray hit distance.
    Float4 values = Float4(0.0f, 0.0f, 0.0f, 0.0f);

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
    sizeof(DeformableHitBarycentric) == sizeof(Float4),
    "DeformableHitBarycentric must stay one aligned vector wide"
);
static_assert(
    alignof(DeformableHitBarycentric) >= alignof(Float4),
    "DeformableHitBarycentric must stay SIMD-aligned"
);

struct alignas(Float4) DeformablePosedHit{
    Core::ECS::EntityID entity = Core::ECS::ENTITY_ID_INVALID;
    RuntimeMeshHandle runtimeMesh;
    u32 editRevision = 0;
    u32 triangle = 0;
    DeformableHitBarycentric bary;
    Float4 position = Float4(0.0f, 0.0f, 0.0f, 1.0f);
    Float4 normal = Float4(0.0f, 0.0f, 1.0f, 0.0f);
    SourceSample restSample;

    [[nodiscard]] f32 distance()const{ return bary.distance(); }
    void setDistance(const f32 value){ bary.setDistance(value); }

    void setPosition(const Float3U& value){
        position = Float4(value.x, value.y, value.z, 1.0f);
    }

    void setNormal(const Float3U& value){
        normal = Float4(value.x, value.y, value.z, 0.0f);
    }
};
static_assert(IsStandardLayout_V<DeformablePosedHit>, "DeformablePosedHit must stay layout-stable");
static_assert(IsTriviallyCopyable_V<DeformablePosedHit>, "DeformablePosedHit must stay cheap to copy");
static_assert(alignof(DeformablePosedHit) >= alignof(Float4), "DeformablePosedHit must stay SIMD-aligned");
static_assert(
    (offsetof(DeformablePosedHit, bary) % alignof(Float4)) == 0,
    "DeformablePosedHit::bary must stay SIMD-aligned"
);
static_assert(
    (offsetof(DeformablePosedHit, position) % alignof(Float4)) == 0,
    "DeformablePosedHit::position must stay SIMD-aligned"
);
static_assert(
    (offsetof(DeformablePosedHit, normal) % alignof(Float4)) == 0,
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
    DeformablePosedHit& outHit,
    Core::Assets::AssetManager* assetManager = nullptr
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

