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


// AVBOIT owns every graph-local task identifier required to declare and validate its transparency stage. The host
// observes the typed first/completion boundary published by transparencyStage() and owns whole-graph execution.
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
// queue checks, and packet-order invariants inside its own domain implementation.
struct RendererAvboitTaskGraphValidation{
    RendererTaskGraphTransparencyStage m_stage;
    bool m_valid = false;

    [[nodiscard]] bool valid()const noexcept{ return m_valid; }
    [[nodiscard]] const RendererTaskGraphTransparencyStage& stage()const noexcept{ return m_stage; }
};

// AVBOIT contributes semantic timing bindings to its host's whole-graph execution. This keeps the domain free to
// change its internal packet topology without taking ownership of graph recording or submission.
inline constexpr usize s_AvboitTaskGraphTimingTicketCapacity = 7u;

struct RendererAvboitTaskGraphTimingTickets{
    Core::GpuTimingSubmissionTicket& m_pre;
    Core::GpuTimingSubmissionTicket& m_depthWarp;
    Core::GpuTimingSubmissionTicket& m_extinction;
    Core::GpuTimingSubmissionTicket& m_integration;
    Core::GpuTimingSubmissionTicket& m_accumulation;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

