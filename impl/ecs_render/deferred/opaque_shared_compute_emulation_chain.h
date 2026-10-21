// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/material/task_graph_compute_emulation_plan.h>
#include <impl/ecs_render/material/task_graph_opaque_compute_tasks.h>
#include <impl/ecs_render/shared/renderer_frame_bindings.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>

#include <core/graphics/gpu_timing.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererMaterialSystem;
struct DeferredGraphResourceImportResult;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct OpaqueSharedComputeEmulationChainInputs{
    DeferredFrameTargets& targets;
    const DeferredGraphResourceImportResult& resources;
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings;
    const ECSRenderDetail::RegularSharedComputeEmulationGraphPlan& plan;
    const Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena>& objectGeometryReads;
    Core::GpuTimingSubmissionTicket*& timingTicket;
    const bool& meshViewSetupReady;
    const bool& sceneShadingSetupReady;
    Optional<Core::GpuTimingMeasure>& sharedComputeEmulationTiming;
    Core::GpuTaskResourceSetUse materialGeometrySetUse{};
    Core::GpuTaskResourceSetUse materialSampledTextureSetUse{};
    Core::BufferRange materialInstanceRange{};
    Core::BufferRange materialTypedRange{};
    Core::GpuGraphResourceId sharedOutput;
    Core::GpuTaskId gbufferTask;
    usize instanceCount = 0u;
    usize materialTypedByteCount = 0u;
    bool materialDrawBuffersUploaded = false;
    bool materialFrameStatesGraphOwned = false;
    bool materialGeometryStatesGraphOwned = false;
};

struct OpaqueSharedComputeEmulationChainResult{
    Core::GpuTaskId completionTask;
    usize taskCount = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Two to five regular opaque draws share one generated-vertex output, so each draw gets an explicit generate then raster phase after the G-buffer.
// The immediate chain keeps every Common -> UAV -> vertex/index state step with the graph, not the material callback.
class OpaqueSharedComputeEmulationChainBuilder final : NoCopy{
public:
    OpaqueSharedComputeEmulationChainBuilder(Core::GpuTaskGraph& graph, RendererMaterialSystem& materialSystem)noexcept
        : m_graph(graph)
        , m_materialSystem(materialSystem)
    {}


public:
    // Writes each declared phase task into `tasks` in order.
    [[nodiscard]] Expected<OpaqueSharedComputeEmulationChainResult> declare(Core::Alloc::ScratchArena& scratchArena, const OpaqueSharedComputeEmulationChainInputs& inputs, Core::GpuTaskId* tasks, usize taskCapacity);


private:
    [[nodiscard]] Expected<Core::GpuTaskId> declarePhase(
        const OpaqueSharedComputeEmulationChainInputs& inputs,
        const Core::GpuTaskSchedulingHint& scheduling,
        const Name& identity,
        AStringView markerLabel,
        const Core::GpuTaskId& dependency,
        ECSRenderDetail::OpaqueRegularSharedComputeEmulationGraphTask::Phase::Enum phase,
        usize drawIndex,
        bool finishTiming,
        const Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena>& resourceUses,
        const Core::GpuTaskResourceSetUse* resourceSetUses,
        usize resourceSetUseCount
    );


private:
    Core::GpuTaskGraph& m_graph;
    RendererMaterialSystem& m_materialSystem;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

