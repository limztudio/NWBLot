// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/shared/task_graph_stage.h>

#include <core/graphics/gpu_timing.h>
#include <core/graphics/task_graph/compiler.h>
#include <core/graphics/task_graph/packet_runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// AVBOIT owns every graph-local task identifier required to declare, validate, and submit its transparency
// stage. The host observes only the typed first/completion boundary published by transparencyStage().
struct RendererAvboitTaskGraphStageState{
    Core::GpuTaskId m_clearFirstTask;
    Core::GpuTaskId m_clearTask;
    Core::GpuTaskId m_transparentCsgIntervalClearFirstTask;
    Core::GpuTaskId m_transparentCsgIntervalClearTask;
    Core::GpuTaskId m_preTask;
    Core::GpuTaskId m_csgReceiverSpanTask;
    Core::GpuTaskId m_csgIntervalCombineTask;
    Core::GpuTaskId m_occupancyStreamTask;
    Core::GpuTaskId m_occupancyComputeEmulationTask;
    Core::GpuTaskId m_occupancySharedComputeEmulationTasks[
        ECSRenderDetail::s_SharedComputeEmulationMaximumPhaseCount
    ] = {};
    usize m_occupancySharedComputeEmulationTaskCount = 0u;
    Core::GpuTaskId m_occupancyTask;
    Core::GpuTaskId m_depthWarpTask;
    Core::GpuTaskId m_extinctionStreamTask;
    Core::GpuTaskId m_extinctionComputeEmulationTask;
    Core::GpuTaskId m_extinctionSharedComputeEmulationTasks[
        ECSRenderDetail::s_SharedComputeEmulationMaximumPhaseCount
    ] = {};
    usize m_extinctionSharedComputeEmulationTaskCount = 0u;
    Core::GpuTaskId m_extinctionTask;
    Core::GpuTaskId m_integrationTask;
    Core::GpuTaskId m_accumulationStreamTask;
    Core::GpuTaskId m_accumulationComputeEmulationTask;
    Core::GpuTaskId m_accumulationSharedComputeEmulationTasks[
        ECSRenderDetail::s_SharedComputeEmulationMaximumPhaseCount
    ] = {};
    usize m_accumulationSharedComputeEmulationTaskCount = 0u;
    Core::GpuTaskId m_accumulationTask;
    Core::GpuTaskId m_accumulationFinalizeTask;

    void reset()noexcept;
    [[nodiscard]] RendererTaskGraphTransparencyStage transparencyStage()const noexcept;
};

// The host sees only this immutable result after graph compilation. AVBOIT keeps the concrete task topology,
// queue checks, and packet-order invariants inside its own domain implementation. The submission endpoint can
// differ from the semantic stage completion: an empty transparency frame completes its consumer dependency at
// Occupancy, while its accepted packet range intentionally terminates at AVBOIT Pre.
struct RendererAvboitTaskGraphValidation{
    RendererTaskGraphTransparencyStage m_stage;
    Core::GpuTaskId m_submissionCompletionTask;
    bool m_valid = false;

    [[nodiscard]] bool valid()const noexcept{ return m_valid; }
    [[nodiscard]] const RendererTaskGraphTransparencyStage& stage()const noexcept{ return m_stage; }
    [[nodiscard]] const Core::GpuTaskId& submissionCompletionTask()const noexcept{ return m_submissionCompletionTask; }
};

// Submission stays inside AVBOIT, but the graph artifact and its frame-owned timing tickets remain supplied by
// the graph host. This keeps the domain free to change its internal packet topology without kernel rewiring.
struct RendererAvboitTaskGraphTimingTickets{
    Core::GpuTimingSubmissionTicket& m_pre;
    Core::GpuTimingSubmissionTicket& m_depthWarp;
    Core::GpuTimingSubmissionTicket& m_extinction;
    Core::GpuTimingSubmissionTicket& m_integration;
    Core::GpuTimingSubmissionTicket& m_accumulation;
};

struct RendererAvboitTaskGraphSubmitContext{
    Core::Device& m_device;
    Core::GpuTaskGraph& m_graph;
    const Core::GpuCompiledGraph& m_compiledGraph;
    const Core::GpuRecordedGraph& m_recordedGraph;
    Core::GpuGraphSubmissionTransaction& m_submissionTransaction;
    RendererAvboitTaskGraphTimingTickets m_timingTickets;
};

struct RendererAvboitTaskGraphSubmission{
    Core::QueueSubmissionToken m_preToken;
    Core::QueueSubmissionToken m_completionToken;
    bool m_submitterAccepted = false;

    [[nodiscard]] bool accepted()const noexcept{
        return m_submitterAccepted && m_preToken.valid() && m_completionToken.valid();
    }
    [[nodiscard]] bool preAccepted()const noexcept{ return m_preToken.valid(); }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
