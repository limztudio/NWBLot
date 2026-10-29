// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/renderer_frame_pipeline.h>
#include <impl/ecs_render/kernel/timing_names.h>

#include <core/task/gpu/frame_timing_begin_task.h>
#include <core/task/gpu/scheduler.h>
#include <core/graphics/gpu_timing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererFramePipeline::declareFrameTimingBeginTask(Core::GpuTimingFrameTransaction& frameTimingTransaction){
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Tiny;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    const auto deferredFrameTimingBeginTaskResult = m_deferredLightingTaskGraph.addTask<Core::FrameTimingBeginGraphTask>(
        Core::GpuTaskDesc{}
            .setIdentity(Name("render.frame_timing_begin"))
            .setMarkerLabel("Frame Timing Begin")
            .setScheduling(scheduling),
        Core::FrameTimingBeginGraphTask::Payload{ .frameTimingTransaction = frameTimingTransaction, .device = m_graphics.getDevice(), .scopeDefinition = RendererGpuTimingScope::s_Frame, }
    );
    if(!deferredFrameTimingBeginTaskResult || !m_deferredLightingTaskGraph.setNormalExecutionPrelude(*deferredFrameTimingBeginTaskResult)){
        if(deferredFrameTimingBeginTaskResult)
            m_deferredFrameTimingBeginTask = *deferredFrameTimingBeginTaskResult;
        else
            m_deferredFrameTimingBeginTask = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare frame timing prelude"));
        return false;
    }
    m_deferredFrameTimingBeginTask = *deferredFrameTimingBeginTaskResult;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererFramePipeline::scheduleDeferredLightingTaskGraphForExecution(Core::Alloc::ScratchArena& scratchArena){
    m_deferredLightingTaskGraphScheduled = false;
    if(!m_deferredLightingTaskGraphDeclared)
        return false;

    Core::GpuTaskGraphCompileOptions compileOptions;
    // A graphics prefix can split immediately after work that enables a different physical queue.
    // This exposes the true cross-queue frontier while preserving the compiler's declaration-derived dependency order.
    compileOptions.packetizationPolicy = Core::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    // Time accepted packets through the presentation endpoint. Readback, history-copy, and recovery tails keep their own policy.
    compileOptions.packetTimingEnvelope.firstTask = m_deferredFrameTimingBeginTask;
    compileOptions.packetTimingEnvelope.lastTask = m_deferredFrameTimingEndTask;
    m_deferredTaskTimingFeedback.configureCompileOptions(compileOptions, m_graphics.getFrameIndex());
    compileOptions.declarationSeconds = m_deferredLightingTaskGraphDeclarationSeconds;

    const Core::GpuTaskScheduler& scheduler = m_graphics.gpuTasks();
    if(!scheduler.scheduleGraph(
        m_deferredLightingTaskGraph,
        compileOptions,
        scratchArena,
        m_deferredLightingTaskGraphAnalysis,
        m_deferredLightingTaskGraphQueueAssignments,
        m_deferredLightingCompiledGraph,
        m_deferredLightingRecordedGraph,
        m_deferredLightingSubmissionTransaction
    )){
        const auto& analysisDiagnostic = m_deferredLightingTaskGraphAnalysis.diagnostic();
        const auto& queueDiagnostic = m_deferredLightingTaskGraphQueueAssignments.diagnostic();
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: deferred graph scheduling failed: analysis={} task={} resource={} queue={} queueTask={}")
            , static_cast<u32>(analysisDiagnostic.status), analysisDiagnostic.task.index, analysisDiagnostic.resource.index
            , static_cast<u32>(queueDiagnostic.status), queueDiagnostic.task.index
        );
        return false;
    }

    const Core::GpuTaskGraph::DeclarationReadView declarations(m_deferredLightingTaskGraph);
    const Core::GpuCompiledGraph::ReadView compiledPlan(m_deferredLightingCompiledGraph);
    if(!compiledPlan.validFor(declarations)){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: deferred graph scheduling lost its compiler plan"));
        return false;
    }
    if(!prepareDeferredGraphPacketEnvelopeMetrics(declarations, compiledPlan, scratchArena))
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not prepare deferred graph packet metrics"));

    m_deferredLightingTaskGraphScheduled = true;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

