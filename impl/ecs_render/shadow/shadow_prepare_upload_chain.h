// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/raytrace/graph_snapshots.h>
#include <impl/ecs_render/deferred/deferred_frame_targets.h>
#include <impl/ecs_render/shadow/types.adhd.h>

#include <core/alloc/scratch.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GraphicsRuntime;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererRayTracingSystem;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ShadowPrepareUploadInputs{
    const DeferredFrameTargets& targets;
    const RayTracingDeferredGraphResourceSnapshot& rayTracingResources;
    Core::GpuGraphResourceId currentBindlessSlots;
    Core::GpuGraphResourceId materialContextSlots;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Frozen selector, context, targets, constants, and scene-BVH uploads precede Shadow Preparation, in that order.
// Tasks declared before a failure stay in `chain`.
class ShadowPrepareUploadChainBuilder final : NoCopy{
public:
    ShadowPrepareUploadChainBuilder(Core::GpuTaskGraph& graph, Core::GraphicsRuntime& graphics, RendererRayTracingSystem& raytracingSystem)noexcept
        : m_graph(graph)
        , m_graphics(graphics)
        , m_raytracingSystem(raytracingSystem)
    {}


public:
    [[nodiscard]] b8 declare(const ShadowPrepareUploadInputs& inputs, Core::Alloc::ScratchArena& scratchArena, ShadowPrepareUploadChain& chain){
        return declareSelectorAndContextUploads(inputs, scratchArena, chain) && declareSurfelFrameConstantsUpload(inputs, chain) && declareShadowMaterialContextUploads(inputs, chain) && declareSceneBvhUploads(inputs, chain);
    }


private:
    [[nodiscard]] b8 declareSelectorAndContextUploads(const ShadowPrepareUploadInputs& inputs, Core::Alloc::ScratchArena& scratchArena, ShadowPrepareUploadChain& chain);
    [[nodiscard]] b8 declareSurfelFrameConstantsUpload(const ShadowPrepareUploadInputs& inputs, ShadowPrepareUploadChain& chain);
    [[nodiscard]] b8 declareShadowMaterialContextUploads(const ShadowPrepareUploadInputs& inputs, ShadowPrepareUploadChain& chain);
    [[nodiscard]] b8 declareSceneBvhUploads(const ShadowPrepareUploadInputs& inputs, ShadowPrepareUploadChain& chain);


private:
    Core::GpuTaskGraph& m_graph;
    Core::GraphicsRuntime& m_graphics;
    RendererRayTracingSystem& m_raytracingSystem;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

