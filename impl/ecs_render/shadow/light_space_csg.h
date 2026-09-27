// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/csg/shadow_cutter_snapshot.h>
#include <impl/assets/graphics/shadow/csg_shadow_constants.h>

#include <core/graphics/rhi/resource.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct InstanceGpuData;
struct NwbRtInstanceMaterialGpu;

namespace ECSRenderDetail{
    struct MeshRayTracingResourceSnapshot;
};

struct LightSpaceCsgInstanceGpu{
    Float34 worldToObject{};
    u32 runtimeBoundsSlot = 0xffffffffu;
    u32 primitiveCount = 0u;
    u32 meshRootSlot = 0xffffffffu;
    u32 padding = 0u;
    Float4 localMin{};
    Float4 localMax{};
};
static_assert(sizeof(LightSpaceCsgInstanceGpu) == NWB_CSG_SHADOW_INSTANCE_BYTES);
static_assert(offsetof(LightSpaceCsgInstanceGpu, runtimeBoundsSlot) == sizeof(Float34));
static_assert(offsetof(LightSpaceCsgInstanceGpu, localMin) == sizeof(Float34) + sizeof(u32) * 4u);
static_assert(IsStandardLayout_V<LightSpaceCsgInstanceGpu>);
static_assert(IsTriviallyCopyable_V<LightSpaceCsgInstanceGpu>);

struct LightSpaceCsgState{
    CsgShadowSnapshot snapshot;
    Vector<CsgShadowReceiverInput, Core::Alloc::GlobalArena> receivers;
    Vector<LightSpaceCsgInstanceGpu, Core::Alloc::GlobalArena> instances;
    Vector<Core::BufferHandle, Core::Alloc::GlobalArena> dynamicBounds;
    Vector<u8, Core::Alloc::GlobalArena> bytes;
    u64 captureGeometryIdentity = 0u;
    bool captureGeometryTrusted = false;
    bool gathering = false;
    bool hardware = false;
    bool limitationLogged = false;


public:
    explicit LightSpaceCsgState(Core::Alloc::GlobalArena& arena)
        : snapshot(arena)
        , receivers(arena)
        , instances(arena)
        , dynamicBounds(arena)
        , bytes(arena)
    {}
};

// Excludes only world transforms; geometry/material edits still invalidate the bounded capture approximation.
[[nodiscard]] u64 BuildLightSpaceCsgCaptureIdentity(
    const LightSpaceCsgState& state,
    const NwbRtInstanceMaterialGpu* materials, const InstanceGpuData* instances, usize instanceCount,
    const u8* materialBytes, usize materialByteCount,
    const Core::TextureHandle* textures, usize textureCount
);

void BeginLightSpaceCsgGather(LightSpaceCsgState& state, Core::ECS::World& world, usize capacity, bool hardware);
void AppendLightSpaceCsgReceiver(
    LightSpaceCsgState& state, Core::ECS::EntityID entity, bool transparent,
    const SIMDMatrix& objectToWorld, const ECSRenderDetail::MeshRayTracingResourceSnapshot& mesh
);
[[nodiscard]] bool FinishLightSpaceCsgGather(
    LightSpaceCsgState& state, Core::ECS::World& world, const CsgShapeRegistry& registry, Core::Alloc::ScratchArena& scratchArena
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

