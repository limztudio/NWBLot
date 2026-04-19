// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "deformable_runtime_mesh_cache.h"

#include <core/ecs/transform_component.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererSystem;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct DeformablePickingRay{
    Float3Data origin = Float3Data(0.0f, 0.0f, 0.0f);
    Float3Data direction = Float3Data(0.0f, 0.0f, 1.0f);
    f32 minDistance = 0.0f;
    f32 maxDistance = Limit<f32>::s_Max;
};

struct DeformablePickingInputs{
    const DeformableMorphWeightsComponent* morphWeights = nullptr;
    const DeformableJointPaletteComponent* jointPalette = nullptr;
    const DeformableDisplacementComponent* displacement = nullptr;
    const Core::ECS::TransformComponent* transform = nullptr;
};

struct DeformablePosedHit{
    Core::ECS::EntityID entity = Core::ECS::ENTITY_ID_INVALID;
    RuntimeMeshHandle runtimeMesh;
    u32 editRevision = 0;
    u32 triangle = 0;
    f32 bary[3] = {};
    f32 distance = 0.0f;
    Float3Data position = Float3Data(0.0f, 0.0f, 0.0f);
    Float3Data normal = Float3Data(0.0f, 0.0f, 1.0f);
    SourceSample restSample;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool BuildDeformablePickingVertices(
    const DeformableRuntimeMeshInstance& instance,
    const DeformablePickingInputs& inputs,
    Vector<DeformableVertexRest>& outVertices
);
[[nodiscard]] bool ResolveDeformableRestSurfaceSample(
    const DeformableRuntimeMeshInstance& instance,
    u32 triangle,
    const f32 (&bary)[3],
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

