// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/shared/frame_types.adhd.h>
#include <impl/ecs_render/avboit/graph_types.adhd.h>

#include <core/graphics/gpu_timing.h>
#include <core/task/gpu/compiler.h>
#include <core/task/gpu/packet_runtime.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// AVBOIT owns its stage identifiers; the host observes the typed boundary and owns execution.
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
    Core::GpuTaskId m_occupancyReusedGeometryProducer;
    Array<Core::GpuTaskId, s_SharedComputeEmulationMaximumPhaseCount> m_occupancySharedComputeEmulationTasks = {};
    usize m_occupancySharedComputeEmulationTaskCount = 0u;
    Core::GpuTaskId m_occupancyTask;
    Core::GpuTaskId m_depthWarpTask;
    Core::GpuTaskId m_extinctionStreamTask;
    Core::GpuTaskId m_extinctionComputeEmulationTask;
    Core::GpuTaskId m_extinctionReusedGeometryProducer;
    Array<Core::GpuTaskId, s_SharedComputeEmulationMaximumPhaseCount> m_extinctionSharedComputeEmulationTasks = {};
    usize m_extinctionSharedComputeEmulationTaskCount = 0u;
    Core::GpuTaskId m_extinctionTask;
    Core::GpuTaskId m_integrationTask;
    Core::GpuTaskId m_accumulationStreamTask;
    Core::GpuTaskId m_accumulationComputeEmulationTask;
    Core::GpuTaskId m_accumulationReusedGeometryProducer;
    Array<Core::GpuTaskId, s_SharedComputeEmulationMaximumPhaseCount> m_accumulationSharedComputeEmulationTasks = {};
    usize m_accumulationSharedComputeEmulationTaskCount = 0u;
    Core::GpuTaskId m_accumulationTask;
    Core::GpuTaskId m_accumulationFinalizeTask;


public:
    void reset()noexcept;
    [[nodiscard]] RendererTaskGraphTransparencyStage transparencyStage()const noexcept;
};

// Host sees only this immutable result; task topology and queue/packet rules stay inside AVBOIT.
struct RendererAvboitTaskGraphValidation{
    RendererTaskGraphTransparencyStage m_stage;
    b8 m_valid = false;


public:
    [[nodiscard]] b8 valid()const noexcept{ return m_valid; }

    [[nodiscard]] const RendererTaskGraphTransparencyStage& stage()const noexcept{ return m_stage; }
};

// AVBOIT contributes timing bindings; domain stays free of recording ownership.
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

