// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/optics/optical_scene_resources.h>
#include <impl/ecs_render/raytrace/graph_snapshots.h>
#include <impl/ecs_render/raytrace/types.adhd.h>
#include <impl/ecs_render/shared/frame_types.adhd.h>

#include <core/graphics/rhi/gpu_descriptor_heap.h>
#include <core/graphics/rhi/raytracing.h>
#include <core/graphics/rhi/resource.h>
#include <core/task/gpu/task_graph.h>

#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Ray-tracing domain owns live scene/material tables; effects retain this snapshot post-preflight plus frozen geometry/texture bundles.
// Schedulable != dispatchable: scene prep must also succeed.
struct RayTracingSceneGraphResources{
    RayTracingSceneContentStamp contentStamp;
    Core::RayTracingAccelStructHandle sceneTlas;
    Core::GpuDescriptorHandle tlasHeapHandle = Core::GpuDescriptorHandle::Invalid();
    Core::BufferHandle materialContextSlotsBuffer;
    Core::BufferHandle instanceMaterialBuffer;
    Core::BufferHandle materialTypedBuffer;
    Core::BufferHandle instanceBuffer;
    Core::BufferHandle csgTraceContextBuffer;
    Span<const Core::BufferHandle> csgTraceBoundsBuffers{};
    RayTracingOpticalSceneSnapshot opticalScene;
    u32 materialContextSlotsHeapSlot = 0u;
    b8 hardwareAvailable = false;


public:
    [[nodiscard]] b8 valid()const noexcept;
};

struct RayTracingSceneGraphReads{
    static constexpr usize s_FixedSceneResourceUseCount = 6u;


public:
    // Ordinary scene reads remain inline; only admitted CSG work allocates the variable posed-bounds list.
    Array<Core::GpuTaskResourceUse, s_FixedSceneResourceUseCount> uses{};
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> csgUses;


public:
    explicit RayTracingSceneGraphReads(Core::Alloc::ScratchArena& scratchArena)noexcept
        : csgUses(scratchArena)
    {}


public:
    [[nodiscard]] b8 valid()const noexcept;
};

// Import common scene reads once per physical resource.
// Geometry buffers and material sampled textures belong to the existing frozen resource sets supplied separately by the caller.
// A failure returns Unexpected without publishing a scene-read bundle.
[[nodiscard]] Expected<RayTracingSceneGraphReads> ImportRayTracingSceneGraphReads(
    const RayTracingSceneGraphResources& resources,
    Core::ResourceStates::Mask tlasInitialState,
    Core::GpuTaskId priorReadersDrained,
    Core::GpuTaskGraph& graph,
    Core::Alloc::ScratchArena& scratchArena
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererFramePipelineDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct TraceResourceSetUses{
    // Trace geometry set and trace material sampled-texture set.
    Array<Core::GpuTaskResourceSetUse, s_TraceResourceSetUseCapacity> uses{};
    usize useCount = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Five raytracing scene/shadow buffers shared by the caustics and shadow-visibility graph declares.
[[nodiscard]] b8 AppendRayTracingSceneShadowBuffers(
    const RayTracingDeferredGraphResourceSnapshot& rayTracingResources,
    Core::GpuTaskGraph& graph,
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena>& resourceUses
);

[[nodiscard]] b8 AppendCurrentCsgRayGeometry(
    const Core::BufferHandle& context,
    Span<const Core::BufferHandle> bounds,
    Core::GpuTaskGraph& graph,
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena>& resourceUses
);

[[nodiscard]] TraceResourceSetUses MakeTraceResourceSetUses(
    Core::GpuGraphResourceSetId traceGeometrySet,
    Core::GpuGraphResourceSetId traceMaterialSampledTextureSet
)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

