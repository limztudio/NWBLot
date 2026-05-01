// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/alloc/scratch.h>
#include <impl/ecs_deformable/deformable_runtime_mesh_cache.h>
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

    [[nodiscard]] SIMDVector originVector()const{
        return LoadFloat(originMinDistance);
    }

    [[nodiscard]] SIMDVector directionVector()const{
        return LoadFloat(directionMaxDistance);
    }

    [[nodiscard]] Float3U origin()const{
        Float3U result;
        StoreFloat(originVector(), &result);
        return result;
    }

    [[nodiscard]] Float3U direction()const{
        Float3U result;
        StoreFloat(directionVector(), &result);
        return result;
    }

    [[nodiscard]] f32 minDistance()const{ return originMinDistance.w; }
    [[nodiscard]] f32 maxDistance()const{ return directionMaxDistance.w; }

    void setOrigin(const SIMDVector value){
        StoreFloat(VectorSetW(value, originMinDistance.w), &originMinDistance);
    }

    void setDirection(const SIMDVector value){
        StoreFloat(VectorSetW(value, directionMaxDistance.w), &directionMaxDistance);
    }

    void setOrigin(const Float3U& value){
        setOrigin(LoadFloat(value));
    }

    void setDirection(const Float3U& value){
        setDirection(LoadFloat(value));
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
    const DeformableSkeletonPoseComponent* skeletonPose = nullptr;
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
    DeformableEditMaskFlags editMaskFlags = s_DeformableEditMaskDefault;
    SourceSample restSample;

    [[nodiscard]] f32 distance()const{ return bary.distance(); }
    void setDistance(const f32 value){ bary.setDistance(value); }

    [[nodiscard]] SIMDVector positionVector()const{
        return LoadFloat(position);
    }

    [[nodiscard]] SIMDVector normalVector()const{
        return LoadFloat(normal);
    }

    void setPosition(const SIMDVector value){
        StoreFloat(VectorSetW(value, 1.0f), &position);
    }

    void setNormal(const SIMDVector value){
        StoreFloat(VectorSetW(value, 0.0f), &normal);
    }

    void setPosition(const Float3U& value){
        setPosition(LoadFloat(value));
    }

    void setNormal(const Float3U& value){
        setNormal(LoadFloat(value));
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
[[nodiscard]] DeformableEditMaskFlags ResolveDeformableTriangleEditMask(
    const DeformableRuntimeMeshInstance& instance,
    u32 triangle
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

