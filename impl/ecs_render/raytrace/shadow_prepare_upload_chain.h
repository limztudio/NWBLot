// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/raytrace/graph_snapshots.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>

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

struct ShadowPrepareUploadTasks{
    Core::GpuTaskId bindlessSlots;
    Core::GpuTaskId rayTraceMaterialContextSlots;
    Core::GpuTaskId csgTraceContext;
    Core::GpuTaskId causticEmissionTargets;
    Core::GpuTaskId surfelFrameConstants;
    Core::GpuTaskId shadowInstanceMaterial;
    Core::GpuTaskId shadowInstance;
    Core::GpuTaskId shadowMaterialTyped;
    Core::GpuTaskId sceneBvhNodes;
    Core::GpuTaskId sceneBvhInstances;
};

struct ShadowPrepareUploadChain{
    ShadowPrepareUploadTasks tasks;
    // Last declared upload; Shadow Preparation depends on it.
    Core::GpuTaskId completionTask;
    // Upload destinations stay invalid when their preflighted buffer is absent.
    Core::GpuGraphResourceId csgTraceContext;
    Core::GpuGraphResourceId causticEmissionTargets;
    Core::GpuGraphResourceId surfelFrameConstants;
    Core::GpuGraphResourceId shadowInstanceMaterials;
    Core::GpuGraphResourceId shadowInstances;
    Core::GpuGraphResourceId shadowMaterialTyped;
    Core::GpuGraphResourceId sceneBvhNodes;
    Core::GpuGraphResourceId sceneBvhInstances;
    bool currentBindlessSlotsGraphOwned = false;
    bool shadowMaterialContextBatchGraphOwned = false;
    bool sceneBvhBatchGraphOwned = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The frozen selector, CSG context, caustic emission-target, surfel-constant, shadow material-context, and scene-BVH uploads that precede Shadow Preparation, chained in that order.
// Tasks declared before a failure stay in `chain`.
class ShadowPrepareUploadChainBuilder final : NoCopy{
public:
    ShadowPrepareUploadChainBuilder(Core::GpuTaskGraph& graph, Core::GraphicsRuntime& graphics, RendererRayTracingSystem& raytracingSystem)noexcept
        : m_graph(graph)
        , m_graphics(graphics)
        , m_raytracingSystem(raytracingSystem)
    {}


public:
    [[nodiscard]] bool declare(Core::Alloc::ScratchArena& scratchArena, const ShadowPrepareUploadInputs& inputs, ShadowPrepareUploadChain& chain);


private:
    [[nodiscard]] Expected<Core::GpuGraphResourceId> importBuffer(const Core::BufferHandle& buffer, const Name& identity, AStringView label);
    [[nodiscard]] bool declareSelectorAndContextUploads(Core::Alloc::ScratchArena& scratchArena, const ShadowPrepareUploadInputs& inputs, ShadowPrepareUploadChain& chain);
    [[nodiscard]] bool declareSurfelFrameConstantsUpload(const ShadowPrepareUploadInputs& inputs, ShadowPrepareUploadChain& chain);
    [[nodiscard]] bool declareShadowMaterialContextUploads(const ShadowPrepareUploadInputs& inputs, ShadowPrepareUploadChain& chain);
    [[nodiscard]] bool declareSceneBvhUploads(const ShadowPrepareUploadInputs& inputs, ShadowPrepareUploadChain& chain);


private:
    Core::GpuTaskGraph& m_graph;
    Core::GraphicsRuntime& m_graphics;
    RendererRayTracingSystem& m_raytracingSystem;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

