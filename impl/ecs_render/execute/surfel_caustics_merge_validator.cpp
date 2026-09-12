// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/execute/surfel_caustics_merge_validator.h>


#include <impl/ecs_render/renderer_frame_pipeline.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SurfelCausticsMergeValidator::SurfelCausticsMergeValidator(NotNull<RendererFramePipeline*> pipeline)
    : m_pipeline(pipeline){
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void SurfelCausticsMergeValidator::validate(
    const Core::GpuCompiledGraph::ReadView& compiledPlan,
    Core::GpuTaskId causticsTask,
    Core::GpuTaskId terminalPresentationTask,
    bool captureLaggedLightingHistory,
    SurfelCausticsMergeValidationResult& outResult
)const{
    RendererFramePipeline& pipeline = *m_pipeline;
    outResult = SurfelCausticsMergeValidationResult{};
    outResult.hardwareCausticsQueue =
        compiledPlan.queueInfoForTask(pipeline.m_deferredHardwareCausticsTask);
    outResult.surfelGiQueue =
        compiledPlan.queueInfoForTask(pipeline.m_deferredSurfelGiTask);
    outResult.surfelGiPreparationQueue =
        compiledPlan.queueInfoForTask(pipeline.m_deferredSurfelGiPreparationTask);
    outResult.surfelGiSnapshotCopyQueue =
        compiledPlan.queueInfoForTask(pipeline.m_deferredSurfelGiSnapshotCopyTask);
    outResult.surfelGiCounterReadbackQueue =
        compiledPlan.queueInfoForTask(pipeline.m_deferredSurfelGiCounterReadbackTask);
    // Keep the clear in GI's packet; a split would escape its endpoint.
    outResult.surfelGiOutputClearMergedIntoGiPacket =
        pipeline.m_deferredSurfelGiIrradianceClearTask.valid()
        && compiledPlan.tasksSharePacket(
            pipeline.m_deferredSurfelGiIrradianceClearTask,
            pipeline.m_deferredSurfelGiTask
        )
    ;
    // Every GI callback must share the semantic packet.
    outResult.surfelGiPreparedPrefixMergedIntoGiPacket =
        (
            !pipeline.m_deferredSurfelGiAgeFreeTask.valid()
            && !pipeline.m_deferredSurfelGiCellHeadClearTask.valid()
            && !pipeline.m_deferredSurfelGiHashBuildTask.valid()
            && !pipeline.m_deferredSurfelGiSpawnTask.valid()
            && !pipeline.m_deferredSurfelGiTraceBuildArgsTask.valid()
            && !pipeline.m_deferredSurfelGiTraceTask.valid()
            && !pipeline.m_deferredSurfelGiResolveTask.valid()
        )
        || (
            pipeline.m_deferredSurfelGiAgeFreeTask.valid()
            && pipeline.m_deferredSurfelGiCellHeadClearTask.valid()
            && pipeline.m_deferredSurfelGiHashBuildTask.valid()
            && pipeline.m_deferredSurfelGiSpawnTask.valid()
            && pipeline.m_deferredSurfelGiTraceBuildArgsTask.valid()
            && pipeline.m_deferredSurfelGiTraceTask.valid()
            && pipeline.m_deferredSurfelGiResolveTask.valid()
            && compiledPlan.tasksSharePacket(
                pipeline.m_deferredSurfelGiAgeFreeTask,
                pipeline.m_deferredSurfelGiTask
            )
            && compiledPlan.tasksSharePacket(
                pipeline.m_deferredSurfelGiCellHeadClearTask,
                pipeline.m_deferredSurfelGiTask
            )
            && compiledPlan.tasksSharePacket(
                pipeline.m_deferredSurfelGiHashBuildTask,
                pipeline.m_deferredSurfelGiTask
            )
            && compiledPlan.tasksSharePacket(
                pipeline.m_deferredSurfelGiSpawnTask,
                pipeline.m_deferredSurfelGiTask
            )
            && compiledPlan.tasksSharePacket(
                pipeline.m_deferredSurfelGiTraceBuildArgsTask,
                pipeline.m_deferredSurfelGiTask
            )
            && compiledPlan.tasksSharePacket(
                pipeline.m_deferredSurfelGiTraceTask,
                pipeline.m_deferredSurfelGiTask
            )
            && compiledPlan.tasksSharePacket(
                pipeline.m_deferredSurfelGiResolveTask,
                pipeline.m_deferredSurfelGiTask
            )
        )
    ;
    // The lifecycle tail must share the first clear's packet.
    outResult.surfelGiInitializationLifecycleMergedIntoPreparationPacket =
        !pipeline.m_deferredSurfelGiInitializationLifecycleTask.valid()
        || (
            pipeline.m_deferredSurfelGiPreparationTask.valid()
            && compiledPlan.tasksSharePacket(
                pipeline.m_deferredSurfelGiPreparationTask,
                pipeline.m_deferredSurfelGiInitializationLifecycleTask
            )
        )
    ;
    // Keep the clear with the producer; caustic callbacks stay one submission.
    outResult.causticPhotonMergedIntoCausticsPacket =
        pipeline.m_deferredCausticPhotonTask.valid()
        && compiledPlan.tasksSharePacket(
            pipeline.m_deferredCausticPhotonTask,
            causticsTask
        )
    ;
    outResult.causticGeometryMergedIntoCausticsPacket =
        pipeline.m_deferredCausticGeometryTask.valid()
        && compiledPlan.tasksSharePacket(
            pipeline.m_deferredCausticGeometryTask,
            causticsTask
        )
    ;
    outResult.causticResolvePrepareMergedIntoCausticsPacket =
        pipeline.m_deferredCausticResolvePrepareTask.valid()
        && compiledPlan.tasksSharePacket(
            pipeline.m_deferredCausticResolvePrepareTask,
            causticsTask
        )
    ;
    outResult.causticResolveWaveletMergedIntoCausticsPacket =
        pipeline.m_deferredCausticResolveWaveletTask.valid()
        && compiledPlan.tasksSharePacket(
            pipeline.m_deferredCausticResolveWaveletTask,
            causticsTask
        )
    ;
    outResult.causticResolveSecondWaveletMergedIntoCausticsPacket =
        pipeline.m_deferredCausticResolveSecondWaveletTask.valid()
        && compiledPlan.tasksSharePacket(
            pipeline.m_deferredCausticResolveSecondWaveletTask,
            causticsTask
        )
    ;
    outResult.causticResolveThirdWaveletMergedIntoCausticsPacket =
        pipeline.m_deferredCausticResolveThirdWaveletTask.valid()
        && compiledPlan.tasksSharePacket(
            pipeline.m_deferredCausticResolveThirdWaveletTask,
            causticsTask
        )
    ;
    outResult.causticResolveFourthWaveletMergedIntoCausticsPacket =
        pipeline.m_deferredCausticResolveFourthWaveletTask.valid()
        && compiledPlan.tasksSharePacket(
            pipeline.m_deferredCausticResolveFourthWaveletTask,
            causticsTask
        )
    ;
    outResult.causticResolveFifthWaveletMergedIntoCausticsPacket =
        pipeline.m_deferredCausticResolveFifthWaveletTask.valid()
        && compiledPlan.tasksSharePacket(
            pipeline.m_deferredCausticResolveFifthWaveletTask,
            causticsTask
        )
    ;
    outResult.causticResolveUpsampleMergedIntoCausticsPacket =
        pipeline.m_deferredCausticResolveUpsampleTask.valid()
        && compiledPlan.tasksSharePacket(
            pipeline.m_deferredCausticResolveUpsampleTask,
            causticsTask
        )
    ;
    outResult.causticIrradianceClearMergedIntoCausticsPacket =
        pipeline.m_deferredCausticIrradianceClearTask.valid()
        && compiledPlan.tasksSharePacket(
            pipeline.m_deferredCausticIrradianceClearTask,
            causticsTask
        )
    ;
    // CPU reset commits only after the shared packet accepts.
    outResult.causticAccumulatorNonTemporalClearMergedIntoCausticsPacket =
        !pipeline.m_deferredCausticAccumulatorNonTemporalClearTask.valid()
        || compiledPlan.tasksSharePacket(
            pipeline.m_deferredCausticAccumulatorNonTemporalClearTask,
            causticsTask
        )
    ;
    // Keep the bootstrap clear in the producer packet; no hidden writers.
    outResult.causticAccumulatorBootstrapClearMergedIntoCausticsPacket =
        !pipeline.m_deferredCausticAccumulatorBootstrapClearTask.valid()
        || compiledPlan.tasksSharePacket(
            pipeline.m_deferredCausticAccumulatorBootstrapClearTask,
            causticsTask
        )
    ;
    // Keep decay in the packet; a split would break the UAV dependency.
    outResult.causticAccumulatorDecayMergedIntoCausticsPacket =
        !pipeline.m_deferredCausticAccumulatorDecayTask.valid()
        || compiledPlan.tasksSharePacket(
            pipeline.m_deferredCausticAccumulatorDecayTask,
            causticsTask
        )
    ;
    // Keep Snapshot Copy and the GI endpoint separate with distinct boundaries.
    outResult.surfelGiSnapshotCopyAndTimingPacketsAreDistinct =
        !pipeline.m_deferredSurfelGiSnapshotCopyTask.valid()
        || !compiledPlan.tasksSharePacket(
            pipeline.m_deferredSurfelGiSnapshotCopyTask,
            pipeline.m_deferredSurfelGiTask
        )
    ;
    outResult.surfelCounterReadbackFollowsPresentation = !pipeline.m_deferredSurfelGiCounterReadbackTask.valid()
        || (
            compiledPlan.taskPrecedesOrSharesPacket(
                terminalPresentationTask,
                pipeline.m_deferredSurfelGiCounterReadbackTask
            )
            && !compiledPlan.tasksSharePacket(
                terminalPresentationTask,
                pipeline.m_deferredSurfelGiCounterReadbackTask
            )
        )
    ;
    outResult.laggedLightingHistoryFollowsPresentation = !captureLaggedLightingHistory
        || (
            compiledPlan.taskPrecedesOrSharesPacket(
                terminalPresentationTask,
                pipeline.m_deferredLaggedLightingHistoryTask
            )
            && !compiledPlan.tasksSharePacket(
                terminalPresentationTask,
                pipeline.m_deferredLaggedLightingHistoryTask
            )
        )
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////