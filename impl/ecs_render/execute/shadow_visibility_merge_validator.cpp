// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/execute/shadow_visibility_merge_validator.h>
#include <impl/ecs_render/renderer_frame_pipeline.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool PreparedShadowVisibilityTasksSharePacket(
    const Core::GpuCompiledGraph::ReadView& compiledPlan,
    const PreparedShadowVisibilityTasks& tasks){
    if(tasks.combinedWavelet && (!tasks.combinedUpsample || !tasks.transparentTemporalMerge.valid()))
        return false;
    if(!tasks.opaque.valid())
        return !tasks.combinedUpsample && !tasks.combinedWavelet;
    if(tasks.combinedUpsample ? tasks.opaqueResolve.valid() : !tasks.opaqueResolve.valid())
        return false;
    const Core::GpuTaskId required[] = {
        tasks.opaque, tasks.opaqueFirstWavelet, tasks.transparentTrace, tasks.transparentFirstWavelet
    };
    for(const Core::GpuTaskId task : required){
        if(!task.valid() || !compiledPlan.tasksSharePacket(tasks.terminal, task))
            return false;
    }
    if(tasks.opaqueResolve.valid() && !compiledPlan.tasksSharePacket(tasks.terminal, tasks.opaqueResolve))
        return false;
    return !tasks.transparentTemporalMerge.valid() || compiledPlan.tasksSharePacket(tasks.terminal, tasks.transparentTemporalMerge);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ShadowVisibilityMergeValidator::ShadowVisibilityMergeValidator(NotNull<RendererFramePipeline*> pipeline)
    : m_pipeline(pipeline){
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ShadowVisibilityMergeValidator::validate(
    const Core::GpuCompiledGraph::ReadView& compiledPlan,
    ShadowVisibilityMergeValidationResult& outResult
)const{
    RendererFramePipeline& pipeline = *m_pipeline;
    outResult = ShadowVisibilityMergeValidationResult{};
    outResult.preparedTasksMerged = PreparedShadowVisibilityTasksSharePacket(
        compiledPlan,
        PreparedShadowVisibilityTasks{
            .terminal = pipeline.m_deferredShadowVisibilityTask,
            .opaque = pipeline.m_deferredShadowVisibilityOpaqueTask,
            .opaqueFirstWavelet = pipeline.m_deferredShadowVisibilityOpaqueFirstWaveletTask,
            .opaqueResolve = pipeline.m_deferredShadowVisibilityOpaqueResolveTask,
            .transparentTrace = pipeline.m_deferredShadowVisibilityTransparentTraceTask,
            .transparentTemporalMerge = pipeline.m_deferredShadowVisibilityTransparentTemporalMergeTask,
            .transparentFirstWavelet = pipeline.m_deferredShadowVisibilityTransparentFirstWaveletTask,
            .combinedUpsample = pipeline.m_deferredShadowCombinedUpsample,
            .combinedWavelet = pipeline.m_deferredShadowCombinedWavelet,
        }
    );
    // Keep the all-lit clear in its packet; split-soft frames keep native clear.
    outResult.allLitClearMerged = pipeline.m_deferredShadowVisibilityOpaqueTask.valid()
        ? !pipeline.m_deferredShadowVisibilityAllLitClearTask.valid()
        : pipeline.m_deferredShadowVisibilityAllLitClearTask.valid()
            && compiledPlan.tasksSharePacket(
                pipeline.m_deferredShadowVisibilityTask,
                pipeline.m_deferredShadowVisibilityAllLitClearTask
            )
    ;
    // Keep the original acceptance endpoint; a split would leak state early.
    outResult.adaptivePrimitivesMerged =
        (!pipeline.m_deferredShadowVisibilityAdaptiveStatsClearTask.valid()
            || compiledPlan.tasksSharePacket(
                pipeline.m_deferredShadowVisibilityTask,
                pipeline.m_deferredShadowVisibilityAdaptiveStatsClearTask
            ))
        && (!pipeline.m_deferredShadowVisibilityAdaptiveCounterClearTask.valid()
            || compiledPlan.tasksSharePacket(
                pipeline.m_deferredShadowVisibilityTask,
                pipeline.m_deferredShadowVisibilityAdaptiveCounterClearTask
            ))
        && (!pipeline.m_deferredShadowVisibilityAdaptiveStatsReadbackTask.valid()
            || compiledPlan.tasksSharePacket(
                pipeline.m_deferredShadowVisibilityTask,
                pipeline.m_deferredShadowVisibilityAdaptiveStatsReadbackTask
            ))
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

