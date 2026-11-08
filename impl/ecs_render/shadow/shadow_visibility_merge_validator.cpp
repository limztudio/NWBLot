// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shadow_visibility_merge_validator.h"

#include <impl/ecs_render/renderer_frame_pipeline.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 PreparedShadowVisibilityTasksSharePacket(const Core::GpuCompiledGraph::ReadView& compiledPlan, const PreparedShadowVisibilityTasks& tasks)noexcept{
    if(tasks.combinedTemporal && (!tasks.combinedWavelet || tasks.opaqueFirstWavelet.valid()))
        return false;
    if(tasks.combinedWavelet && (!tasks.combinedUpsample || !tasks.transparentTemporalMerge.valid()))
        return false;
    if(!tasks.opaque.valid())
        return !tasks.combinedUpsample && !tasks.combinedWavelet && !tasks.combinedTemporal && !tasks.diagnostics.valid();
    if(tasks.combinedUpsample ? tasks.opaqueResolve.valid() : !tasks.opaqueResolve.valid())
        return false;
    const Array<Core::GpuTaskId, 3> required = { tasks.opaque, tasks.transparentTrace, tasks.transparentFirstWavelet };
    for(const Core::GpuTaskId task : required){
        if(!task.valid() || !compiledPlan.tasksSharePacket(tasks.terminal, task))
            return false;
    }
    if(!tasks.combinedTemporal && (!tasks.opaqueFirstWavelet.valid() || !compiledPlan.tasksSharePacket(tasks.terminal, tasks.opaqueFirstWavelet)))
        return false;
    if(tasks.opaqueResolve.valid() && !compiledPlan.tasksSharePacket(tasks.terminal, tasks.opaqueResolve))
        return false;
    if(tasks.diagnostics.valid() && !compiledPlan.tasksSharePacket(tasks.terminal, tasks.diagnostics))
        return false;
    return !tasks.transparentTemporalMerge.valid() || compiledPlan.tasksSharePacket(tasks.terminal, tasks.transparentTemporalMerge);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ShadowVisibilityMergeValidator::validate(const Core::GpuCompiledGraph::ReadView& compiledPlan, ShadowVisibilityMergeValidationResult& outResult)const noexcept{
    const RendererFramePipeline& pipeline = m_pipeline;
    outResult = ShadowVisibilityMergeValidationResult{};
    PreparedShadowVisibilityTasks tasks;
    tasks.terminal = pipeline.m_shadowVisibilityGraph.m_deferredShadowVisibilityTask;
    tasks.opaque = pipeline.m_shadowVisibilityGraph.m_deferredShadowVisibilityOpaqueTask;
    tasks.opaqueFirstWavelet = pipeline.m_shadowVisibilityGraph.m_deferredShadowVisibilityOpaqueFirstWaveletTask;
    tasks.opaqueResolve = pipeline.m_shadowVisibilityGraph.m_deferredShadowVisibilityOpaqueResolveTask;
    tasks.transparentTrace = pipeline.m_shadowVisibilityGraph.m_deferredShadowVisibilityTransparentTraceTask;
    tasks.transparentTemporalMerge = pipeline.m_shadowVisibilityGraph.m_deferredShadowVisibilityTransparentTemporalMergeTask;
    tasks.transparentFirstWavelet = pipeline.m_shadowVisibilityGraph.m_deferredShadowVisibilityTransparentFirstWaveletTask;
    tasks.diagnostics = pipeline.m_shadowVisibilityGraph.m_deferredShadowVisibilityDiagnosticsTask;
    tasks.combinedUpsample = pipeline.m_shadowVisibilityGraph.m_deferredShadowCombinedUpsample;
    tasks.combinedWavelet = pipeline.m_shadowVisibilityGraph.m_deferredShadowCombinedWavelet;
    tasks.combinedTemporal = pipeline.m_shadowVisibilityGraph.m_deferredShadowCombinedTemporal;
    outResult.preparedTasksMerged = PreparedShadowVisibilityTasksSharePacket(compiledPlan, tasks);
    // Keep the all-lit clear in its packet; split-soft frames keep native clear.
    outResult.allLitClearMerged = pipeline.m_shadowVisibilityGraph.m_deferredShadowVisibilityOpaqueTask.valid()
        ? !pipeline.m_shadowVisibilityGraph.m_deferredShadowVisibilityAllLitClearTask.valid()
        : pipeline.m_shadowVisibilityGraph.m_deferredShadowVisibilityAllLitClearTask.valid()
            && compiledPlan.tasksSharePacket(pipeline.m_shadowVisibilityGraph.m_deferredShadowVisibilityTask, pipeline.m_shadowVisibilityGraph.m_deferredShadowVisibilityAllLitClearTask)
    ;
    // The append-counter clear must precede visibility in the same packet.
    outResult.adaptivePrimitivesMerged = !pipeline.m_shadowVisibilityGraph.m_deferredShadowVisibilityAdaptiveCounterClearTask.valid() || compiledPlan.tasksSharePacket(pipeline.m_shadowVisibilityGraph.m_deferredShadowVisibilityTask, pipeline.m_shadowVisibilityGraph.m_deferredShadowVisibilityAdaptiveCounterClearTask);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

