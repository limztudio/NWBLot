// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "surfel_caustics_merge_validator.h"

#include <impl/ecs_render/renderer_frame_pipeline.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void SurfelCausticsMergeValidator::validate(
    const Core::GpuCompiledGraph::ReadView& compiledPlan,
    Core::GpuTaskId causticsTask,
    Core::GpuTaskId terminalPresentationTask,
    b8 captureLaggedLightingHistory,
    SurfelCausticsMergeValidationResult& outResult
)const noexcept{
    const RendererFramePipeline& pipeline = m_pipeline;
    outResult = SurfelCausticsMergeValidationResult{};
    outResult.hardwareCausticsQueue = compiledPlan.queueInfoForTask(pipeline.m_causticsGraph.m_deferredHardwareCausticsTask);
    outResult.surfelGiQueue = compiledPlan.queueInfoForTask(pipeline.m_surfelGiGraph.m_deferredSurfelGiTask);
    outResult.surfelGiPreparationQueue = compiledPlan.queueInfoForTask(pipeline.m_surfelGiGraph.m_deferredSurfelGiPreparationTask);
    outResult.surfelGiSnapshotCopyQueue = compiledPlan.queueInfoForTask(pipeline.m_surfelGiGraph.m_deferredSurfelGiSnapshotCopyTask);
    outResult.surfelGiCounterReadbackQueue = compiledPlan.queueInfoForTask(pipeline.m_surfelGiGraph.m_deferredSurfelGiCounterReadbackTask);
    // Keep the clear in GI's packet; a split would escape its endpoint.
    outResult.surfelGiOutputClearMergedIntoGiPacket = pipeline.m_surfelGiGraph.m_deferredSurfelGiIrradianceClearTask.valid() && compiledPlan.tasksSharePacket(pipeline.m_surfelGiGraph.m_deferredSurfelGiIrradianceClearTask, pipeline.m_surfelGiGraph.m_deferredSurfelGiTask);
    // Every GI callback must share the semantic packet.
    outResult.surfelGiPreparedPrefixMergedIntoGiPacket =
        (
            !pipeline.m_surfelGiGraph.m_deferredSurfelGiAgeFreeTask.valid()
            && !pipeline.m_surfelGiGraph.m_deferredSurfelGiCellHeadClearTask.valid()
            && !pipeline.m_surfelGiGraph.m_deferredSurfelGiHashBuildTask.valid()
            && !pipeline.m_surfelGiGraph.m_deferredSurfelGiSpawnTask.valid()
            && !pipeline.m_surfelGiGraph.m_deferredSurfelGiTraceBuildArgsTask.valid()
            && !pipeline.m_surfelGiGraph.m_deferredSurfelGiTraceTask.valid()
            && !pipeline.m_surfelGiGraph.m_deferredSurfelGiResolveTask.valid()
        )
        || (
            pipeline.m_surfelGiGraph.m_deferredSurfelGiAgeFreeTask.valid()
            && pipeline.m_surfelGiGraph.m_deferredSurfelGiCellHeadClearTask.valid()
            && pipeline.m_surfelGiGraph.m_deferredSurfelGiHashBuildTask.valid()
            && pipeline.m_surfelGiGraph.m_deferredSurfelGiSpawnTask.valid()
            && pipeline.m_surfelGiGraph.m_deferredSurfelGiTraceBuildArgsTask.valid()
            && pipeline.m_surfelGiGraph.m_deferredSurfelGiTraceTask.valid()
            && pipeline.m_surfelGiGraph.m_deferredSurfelGiResolveTask.valid()
            && compiledPlan.tasksSharePacket(pipeline.m_surfelGiGraph.m_deferredSurfelGiAgeFreeTask, pipeline.m_surfelGiGraph.m_deferredSurfelGiTask)
            && compiledPlan.tasksSharePacket(pipeline.m_surfelGiGraph.m_deferredSurfelGiCellHeadClearTask, pipeline.m_surfelGiGraph.m_deferredSurfelGiTask)
            && compiledPlan.tasksSharePacket(pipeline.m_surfelGiGraph.m_deferredSurfelGiHashBuildTask, pipeline.m_surfelGiGraph.m_deferredSurfelGiTask)
            && compiledPlan.tasksSharePacket(pipeline.m_surfelGiGraph.m_deferredSurfelGiSpawnTask, pipeline.m_surfelGiGraph.m_deferredSurfelGiTask)
            && compiledPlan.tasksSharePacket(pipeline.m_surfelGiGraph.m_deferredSurfelGiTraceBuildArgsTask, pipeline.m_surfelGiGraph.m_deferredSurfelGiTask)
            && compiledPlan.tasksSharePacket(pipeline.m_surfelGiGraph.m_deferredSurfelGiTraceTask, pipeline.m_surfelGiGraph.m_deferredSurfelGiTask)
            && compiledPlan.tasksSharePacket(pipeline.m_surfelGiGraph.m_deferredSurfelGiResolveTask, pipeline.m_surfelGiGraph.m_deferredSurfelGiTask)
        )
    ;
    // The lifecycle tail must share the first clear's packet.
    outResult.surfelGiInitializationLifecycleMergedIntoPreparationPacket =
        !pipeline.m_surfelGiGraph.m_deferredSurfelGiInitializationLifecycleTask.valid()
        || (pipeline.m_surfelGiGraph.m_deferredSurfelGiPreparationTask.valid() && compiledPlan.tasksSharePacket(pipeline.m_surfelGiGraph.m_deferredSurfelGiPreparationTask, pipeline.m_surfelGiGraph.m_deferredSurfelGiInitializationLifecycleTask))
    ;
    // Keep the clear with the producer; caustic callbacks stay one submission.
    outResult.causticPhotonMergedIntoCausticsPacket = pipeline.m_causticsGraph.m_deferredCausticPhotonTask.valid() && compiledPlan.tasksSharePacket(pipeline.m_causticsGraph.m_deferredCausticPhotonTask, causticsTask);
    outResult.causticGeometryMergedIntoCausticsPacket = pipeline.m_causticsGraph.m_deferredCausticGeometryTask.valid() && compiledPlan.tasksSharePacket(pipeline.m_causticsGraph.m_deferredCausticGeometryTask, causticsTask);
    outResult.causticResolvePrepareMergedIntoCausticsPacket = pipeline.m_causticsGraph.m_deferredCausticResolvePrepareTask.valid() && compiledPlan.tasksSharePacket(pipeline.m_causticsGraph.m_deferredCausticResolvePrepareTask, causticsTask);
    outResult.causticResolveWaveletsMergedIntoCausticsPacket = true;
    for(const Core::GpuTaskId waveletTask : pipeline.m_causticsGraph.m_deferredCausticResolveWaveletTasks){
        if(!waveletTask.valid() || !compiledPlan.tasksSharePacket(waveletTask, causticsTask)){
            outResult.causticResolveWaveletsMergedIntoCausticsPacket = false;
            break;
        }
    }
    outResult.causticResolveUpsampleMergedIntoCausticsPacket = pipeline.m_causticsGraph.m_deferredCausticResolveUpsampleTask.valid() && compiledPlan.tasksSharePacket(pipeline.m_causticsGraph.m_deferredCausticResolveUpsampleTask, causticsTask);
    outResult.causticIrradianceClearMergedIntoCausticsPacket = pipeline.m_causticsGraph.m_deferredCausticIrradianceClearTask.valid() && compiledPlan.tasksSharePacket(pipeline.m_causticsGraph.m_deferredCausticIrradianceClearTask, causticsTask);
    // Keep the bootstrap clear in the producer packet; no hidden writers.
    outResult.causticAccumulatorBootstrapClearMergedIntoCausticsPacket = !pipeline.m_causticsGraph.m_deferredCausticAccumulatorBootstrapClearTask.valid() || compiledPlan.tasksSharePacket(pipeline.m_causticsGraph.m_deferredCausticAccumulatorBootstrapClearTask, causticsTask);
    // Keep decay in the packet; a split would break the UAV dependency.
    outResult.causticAccumulatorDecayMergedIntoCausticsPacket = !pipeline.m_causticsGraph.m_deferredCausticAccumulatorDecayTask.valid() || compiledPlan.tasksSharePacket(pipeline.m_causticsGraph.m_deferredCausticAccumulatorDecayTask, causticsTask);
    outResult.surfelGiSnapshotCopyAndTimingPacketsAreDistinct = !pipeline.m_surfelGiGraph.m_deferredSurfelGiSnapshotCopyTask.valid() || !compiledPlan.tasksSharePacket(pipeline.m_surfelGiGraph.m_deferredSurfelGiSnapshotCopyTask, pipeline.m_surfelGiGraph.m_deferredSurfelGiTask);
    outResult.surfelCounterReadbackFollowsPresentation = !pipeline.m_surfelGiGraph.m_deferredSurfelGiCounterReadbackTask.valid()
        || (compiledPlan.taskPrecedesOrSharesPacket(terminalPresentationTask, pipeline.m_surfelGiGraph.m_deferredSurfelGiCounterReadbackTask) && !compiledPlan.tasksSharePacket(terminalPresentationTask, pipeline.m_surfelGiGraph.m_deferredSurfelGiCounterReadbackTask))
    ;
    outResult.laggedLightingHistoryFollowsPresentation = !captureLaggedLightingHistory
        || (compiledPlan.taskPrecedesOrSharesPacket(terminalPresentationTask, pipeline.m_deferredLaggedLightingHistoryTask) && !compiledPlan.tasksSharePacket(terminalPresentationTask, pipeline.m_deferredLaggedLightingHistoryTask))
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

