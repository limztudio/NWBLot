// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/execute/shadow_visibility_merge_validator.h>


#include <impl/ecs_render/renderer_frame_pipeline.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


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
    outResult.preparedTasksMerged =
        !pipeline.m_deferredShadowVisibilityOpaqueTask.valid()
        || (
            compiledPlan.tasksSharePacket(
                pipeline.m_deferredShadowVisibilityTask,
                pipeline.m_deferredShadowVisibilityOpaqueTask
            )
            && pipeline.m_deferredShadowVisibilityOpaqueFirstWaveletTask.valid()
            && compiledPlan.tasksSharePacket(
                pipeline.m_deferredShadowVisibilityTask,
                pipeline.m_deferredShadowVisibilityOpaqueFirstWaveletTask
            )
            && pipeline.m_deferredShadowVisibilityOpaqueResolveTask.valid()
            && compiledPlan.tasksSharePacket(
                pipeline.m_deferredShadowVisibilityTask,
                pipeline.m_deferredShadowVisibilityOpaqueResolveTask
            )
            && pipeline.m_deferredShadowVisibilityTransparentTraceTask.valid()
            && compiledPlan.tasksSharePacket(
                pipeline.m_deferredShadowVisibilityTask,
                pipeline.m_deferredShadowVisibilityTransparentTraceTask
            )
            && (
                !pipeline.m_deferredShadowVisibilityTransparentTemporalMergeTask.valid()
                || compiledPlan.tasksSharePacket(
                    pipeline.m_deferredShadowVisibilityTask,
                    pipeline.m_deferredShadowVisibilityTransparentTemporalMergeTask
                )
            )
            && pipeline.m_deferredShadowVisibilityTransparentFirstWaveletTask.valid()
            && compiledPlan.tasksSharePacket(
                pipeline.m_deferredShadowVisibilityTask,
                pipeline.m_deferredShadowVisibilityTransparentFirstWaveletTask
            )
        )
    ;
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
