// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/shadow/light_space_settings.adhd.h>
#include <impl/ecs_render/csg/shadow_cutter_snapshot.h>
#include <impl/ecs_render/mesh/renderer_mesh_types.h>
#include <impl/ecs_render/raytrace/instance_material.h>
#include <impl/assets/graphics/shadow/csg_shadow_constants.h>

#include <core/graphics/rhi/gpu_descriptor_heap.h>
#include <core/graphics/rhi/resource.h>

#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{
    struct MeshRayTracingResourceSnapshot;
};

static_assert(sizeof(LightSpaceCsgInstanceGpu) == NWB_CSG_RAY_INSTANCE_BYTES);
static_assert(offsetof(LightSpaceCsgInstanceGpu, runtimeBoundsSlot) == sizeof(Float34));
static_assert(offsetof(LightSpaceCsgInstanceGpu, localMin) == sizeof(Float34) + sizeof(u32) * 4u);
static_assert(IsStandardLayout_V<LightSpaceCsgInstanceGpu>);
static_assert(IsTriviallyCopyable_V<LightSpaceCsgInstanceGpu>);

struct LightSpaceCsgState{
    CsgShadowSnapshot snapshot;
    Vector<CsgShadowReceiverInput, Core::Alloc::GlobalArena> receivers;
    Vector<LightSpaceCsgInstanceGpu, Core::Alloc::GlobalArena> instances;
    Vector<Core::BufferHandle, Core::Alloc::GlobalArena> dynamicBounds;
    // Current CPU context uploads into independently retained shadow and GI buffers.
    Vector<u8, Core::Alloc::GlobalArena> bytes;
    Vector<Span<const u32>, Core::Alloc::GlobalArena> solidTriangleMasks;
    u64 captureGeometryIdentity = 0u;
    b8 captureGeometryTrusted = false;
    b8 gathering = false;
    b8 hardware = false;
    b8 limitationLogged = false;


public:
    explicit LightSpaceCsgState(Core::Alloc::GlobalArena& arena)noexcept
        : snapshot(arena)
        , receivers(arena)
        , instances(arena)
        , dynamicBounds(arena)
        , bytes(arena)
        , solidTriangleMasks(arena)
    {}
};

// Excludes only world transforms; geometry/material edits still invalidate the bounded capture approximation.
[[nodiscard]] u64 BuildLightSpaceCsgCaptureIdentity(
    const LightSpaceCsgState& state,
    Span<const NwbRtInstanceMaterialGpu> materials, Span<const InstanceGpuData> instances,
    Span<const u8> materialBytes,
    Span<const Core::TextureHandle> textures
)noexcept;

void BeginLightSpaceCsgGather(LightSpaceCsgState& state, Core::ECS::World& world, usize capacity, b8 hardware);
void AppendLightSpaceCsgReceiver(LightSpaceCsgState& state, Core::ECS::EntityID entity, b8 transparent, const SIMDMatrix& objectToWorld, const SIMDMatrix& worldToObject, SIMDVectorSingle localMin, SIMDVectorSingle localMax, const ECSRenderDetail::MeshRayTracingResourceSnapshot& mesh);
[[nodiscard]] b8 FinishLightSpaceCsgGather(LightSpaceCsgState& state, Core::ECS::World& world, const CsgShapeRegistry& registry, Core::Alloc::ScratchArena& scratchArena);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
