// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/deferred/frame_tail_builder.h>


#include <core/graphics/vulkan/backend.h>

#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/kernel/task_graph_frame_recovery_task.h>
#include <impl/ecs_render/kernel/task_graph_queue_requests.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/renderer_frame_pipeline.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


DeferredFrameTailBuilder::DeferredFrameTailBuilder(NotNull<RendererFramePipeline*> pipeline)
    : m_pipeline(pipeline){
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool DeferredFrameTailBuilder::declare(
    const DeferredFrameTailInputs& inputs,
    DeferredFrameTailResult& outResult
){
    using namespace RendererTaskGraphDetail;
    RendererFramePipeline& pipeline = *m_pipeline;
    outResult = DeferredFrameTailResult{};
    if(!inputs.surfelResources)
        return false;
    if(!inputs.frameTimingTransaction)
        return false;
    if(!inputs.declarationBegin)
        return false;

    // Keep this diagnostic behind the terminal presentation endpoint so whole-normal execution cannot absorb its
    // independent Transfer-preferred tail and it cannot delay lighting or presentation.
    pipeline.declareDeferredSurfelCountReadbackTask(*inputs.surfelResources);

    if(inputs.capturesLaggedLightingHistory){
        // The core built-in derives whole-resource CopySource/CopyDest declarations for these regions and retains
        // the imports itself. The array slices stay explicit only in the native copy body.
        Core::GpuCopyTextureTaskRegion historyCopyRegions[NWB_SCENE_SHADOW_SLOT_COUNT + 2u] = {};
        for(u32 shadowSlot = 0u; shadowSlot < NWB_SCENE_SHADOW_SLOT_COUNT; ++shadowSlot){
            Core::GpuCopyTextureTaskRegion& region = historyCopyRegions[shadowSlot];
            region.source = inputs.historyCopyShadowVisibility;
            region.destination = inputs.historyCopyDestinationShadowVisibility;
            region.sourceSlice.setArraySlice(shadowSlot);
            region.destinationSlice.setArraySlice(shadowSlot);
        }
        historyCopyRegions[NWB_SCENE_SHADOW_SLOT_COUNT].source = inputs.historyCopyCausticIrradiance;
        historyCopyRegions[NWB_SCENE_SHADOW_SLOT_COUNT].destination = inputs.historyCopyDestinationCausticIrradiance;
        historyCopyRegions[NWB_SCENE_SHADOW_SLOT_COUNT + 1u].source = inputs.historyCopySurfelIrradiance;
        historyCopyRegions[NWB_SCENE_SHADOW_SLOT_COUNT + 1u].destination = inputs.historyCopyDestinationSurfelIrradiance;
        Core::GpuTaskSchedulingHint historyCopyScheduling;
        historyCopyScheduling.cost = Core::GpuTaskCostHint::Medium;
        historyCopyScheduling.forceSubmissionBoundary = true;
        historyCopyScheduling.allowPacketMerge = false;
        const Core::GpuTaskId historyCopyDependencies[] = { pipeline.m_deferredFrameTimingEndTask };
        Core::GpuTaskDesc historyCopyDesc;
        historyCopyDesc
            .setIdentity(Name("render.lagged_history_copy"))
            .setMarkerLabel("Lagged Lighting History Copy")
            .setQueue(TransferQueueRequest())
            .setScheduling(historyCopyScheduling)
            .setDependencies(historyCopyDependencies, LengthOf(historyCopyDependencies))
        ;
        pipeline.m_deferredLaggedLightingHistoryTask = pipeline.m_deferredLightingTaskGraph.addCopyTextureTask(
            historyCopyDesc,
            Core::GpuCopyTextureTaskDesc{
                .regions = historyCopyRegions,
                .regionCount = LengthOf(historyCopyRegions),
                .acceptedToken = &pipeline.m_laggedLightingHistorySubmissionToken,
            }
        );
        if(!pipeline.m_deferredLaggedLightingHistoryTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred lagged-lighting history-copy task"));
            return false;
        }
    }

    // Recovery is a late independent Graphics tail. It deliberately has no packet dependency on normal work: a
    // rejected suffix must not prevent it from retiring the accepted frame prefix. Its compiled packet asks the
    // graph transaction to join every accepted non-Graphics physical queue at submit time.
    const Core::GpuGraphResourceId recoveryDomain = pipeline.m_deferredLightingTaskGraph.importHazardDomain(
        HazardDomainDesc(Name("render.frame_recovery.timing"), "Frame Recovery Timing")
    );
    if(!recoveryDomain.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import deferred frame-recovery graph resources"));
        return false;
    }

    const Core::GpuTaskResourceUse recoveryResourceUses[] = {
        ReadWriteUse(recoveryDomain, Core::ResourceStates::Common),
    };
    Core::GpuTaskSchedulingHint recoveryScheduling;
    recoveryScheduling.cost = Core::GpuTaskCostHint::Tiny;
    recoveryScheduling.forceSubmissionBoundary = true;
    recoveryScheduling.allowPacketMerge = false;
    recoveryScheduling.joinsAcceptedQueueFrontier = true;
    recoveryScheduling.isRecoverySubmission = true;
    Core::GpuTaskDesc recoveryDesc;
    recoveryDesc
        .setIdentity(Name("render.frame_recovery"))
        .setMarkerLabel("Frame Recovery")
        .setQueue(GraphicsQueueRequest())
        .setScheduling(recoveryScheduling)
        .setResourceUses(recoveryResourceUses, LengthOf(recoveryResourceUses))
    ;
    pipeline.m_deferredFrameRecoveryTask = pipeline.m_deferredLightingTaskGraph.addTask<ECSRenderDetail::FrameRecoveryGraphTask>(
        recoveryDesc,
        ECSRenderDetail::FrameRecoveryGraphTask::Payload{
            .frameTimingTransaction = inputs.frameTimingTransaction,
            .armed = &pipeline.m_deferredFrameRecoveryArmed,
            .retiresFrameTiming = &pipeline.m_deferredFrameRecoveryRetiresTiming,
        }
    );
    if(!pipeline.m_deferredFrameRecoveryTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred frame-recovery graph task"));
        return false;
    }

    // The backend owns physical queue discovery and identity. The renderer consumes this immutable view directly so
    // graph packets can target multiple same-class native queues without rebuilding a class-shaped topology here.
    const auto& device = pipeline.m_graphics.getDevice();
    const Core::GpuTaskGraphQueueTopology topology = device.getPhysicalQueueTopology();
    if(!topology.queues || topology.queueCount == 0u){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: no native physical queue registry is available for the deferred graph"));
        return false;
    }
    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_TaskGraphArena);
    const Core::GpuTaskGraphCompiler compiler;
    Core::GpuTaskGraphCompileOptions compileOptions;
    // A graphics prefix can now split immediately after work that enables a different physical queue. This exposes
    // the true cross-queue frontier while preserving the compiler's declaration-derived dependency order.
    compileOptions.packetizationPolicy = Core::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    // Time the accepted normal-rendering packets from the packet containing frame-timing begin through the graph-owned
    // presentation endpoint. Late readback, history-copy, and recovery tails retain separate diagnostic/lifecycle policy.
    compileOptions.packetTimingEnvelope.firstTask = pipeline.m_deferredShadowPrepareTask;
    compileOptions.packetTimingEnvelope.lastTask = pipeline.m_deferredFrameTimingEndTask;
    pipeline.m_deferredTaskTimingFeedback.configureCompileOptions(compileOptions, pipeline.m_graphics.getFrameIndex());
    compileOptions.declarationSeconds = DurationInSeconds<f64>(TimerNow(), *inputs.declarationBegin);
    const Core::GpuTaskGraph::DeclarationReadView declarations(pipeline.m_deferredLightingTaskGraph);
    if(!compiler.compile(
        declarations,
        pipeline.m_deferredLightingTaskGraphAnalysis,
        topology,
        pipeline.m_deferredLightingTaskGraphQueueAssignments,
        pipeline.m_deferredLightingCompiledGraph,
        scratchArena,
        compileOptions
    )){
        const auto& analysisDiagnostic = pipeline.m_deferredLightingTaskGraphAnalysis.diagnostic();
        const auto& queueDiagnostic = pipeline.m_deferredLightingTaskGraphQueueAssignments.diagnostic();
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: deferred graph compilation failed: analysis={} task={} resource={} queue={} queueTask={}")
            , static_cast<u32>(analysisDiagnostic.status), analysisDiagnostic.task.index, analysisDiagnostic.resource.index
            , static_cast<u32>(queueDiagnostic.status), queueDiagnostic.task.index
        );
        return false;
    }
    const Core::GpuCompiledGraph::ReadView compiledPlan(pipeline.m_deferredLightingCompiledGraph);
    if(
        !compiledPlan.validFor(declarations)
        || !pipeline.prepareDeferredGraphPacketEnvelopeMetrics(declarations, compiledPlan, scratchArena)
    )
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not prepare deferred graph packet metrics"));
    pipeline.m_deferredLightingRecordedGraph.reset(pipeline.m_deferredLightingCompiledGraph);
    pipeline.m_deferredLightingSubmissionTransaction.reset(pipeline.m_deferredLightingCompiledGraph);
    pipeline.m_deferredLightingTaskGraphValid = true;
    outResult.compiled = true;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
