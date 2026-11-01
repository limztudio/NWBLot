// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_execute_lifecycle.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 FrameExecuteLifecycle::PrepareShadowPrepareTask(void* const rawContext, const Core::CommandListResourceStateHandoff* const finalState){
    FrameExecuteLifecycle::ShadowPrepareStateLifecycleContext* const context = static_cast<ShadowPrepareStateLifecycleContext*>(rawContext);
    if(!context || !finalState)
        return false;
    const b8 candidateBuilt = context->renderer->m_shadowPreparePersistentState.buildMergedBufferSubset(*finalState, context->buffers, context->scratchArena, *context->stateCandidate);
    const b8 candidatePresent = context->stateCandidate->valid() && !context->stateCandidate->empty();
    context->statePrepared = candidateBuilt && (candidatePresent || !context->stateCandidateRequired);
    return context->statePrepared;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 FrameExecuteLifecycle::AcceptShadowPrepareTask(void* const rawContext, [[maybe_unused]] const Core::QueueSubmissionToken& token){
    FrameExecuteLifecycle::ShadowPrepareStateLifecycleContext* const context = static_cast<ShadowPrepareStateLifecycleContext*>(rawContext);
    if(!context || !context->statePrepared)
        return false;

    RendererFramePipeline& renderer = *context->renderer;
    if(!context->stateCandidate->valid() || (context->stateCandidate->empty() && context->stateCandidateRequired)){
        context->stateReady = false;
        renderer.m_raytracingSystem.discardPreflightShadowVisibilityResources();
        return false;
    }

    context->stateReady = renderer.m_shadowPreparePersistentState.commit(*context->stateCandidate);
    if(!context->stateReady){
        renderer.m_raytracingSystem.discardPreflightShadowVisibilityResources();
        return false;
    }
    renderer.m_raytracingSystem.confirmPreparedSceneTlasBuild();
    renderer.m_raytracingSystem.confirmPreparedMeshBlasBuilds();
    renderer.m_raytracingSystem.confirmPreparedMeshSwBvhBuilds();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 FrameExecuteLifecycle::PrepareShadowVisibilityTask(void* const rawContext, const Core::CommandListResourceStateHandoff* const finalState){
    FrameExecuteLifecycle::ShadowVisibilityStateLifecycleContext* const context = static_cast<ShadowVisibilityStateLifecycleContext*>(rawContext);
    if(!context || context->returnTextures.empty() || context->scratchTextures.empty() || context->scratchBuffers.empty() || !finalState)
        return false;

    // An inactive route retains its live allocation and accepted native state until that route runs again.
    const b8 scratchStateReady = context->renderer->m_shadowComputePersistentState.buildMergedResourceSubset(
        *finalState,
        context->scratchTextures,
        context->scratchBuffers,
        context->scratchArena,
        *context->scratchStateCandidate
    );
    const b8 returnStateReady = context->renderer->m_shadowVisibilityReturnState.buildFilteredResourceSubset(*finalState, context->returnTextures, {}, context->scratchArena, *context->returnStateCandidate);
    context->statePrepared = returnStateReady && scratchStateReady;
    return context->statePrepared;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 FrameExecuteLifecycle::AcceptShadowVisibilityTask(void* const rawContext, [[maybe_unused]] const Core::QueueSubmissionToken& token){
    FrameExecuteLifecycle::ShadowVisibilityStateLifecycleContext* const context = static_cast<ShadowVisibilityStateLifecycleContext*>(rawContext);
    if(!context || !context->statePrepared)
        return false;

    const b8 returnStateReady = context->renderer->m_shadowVisibilityReturnState.commit(*context->returnStateCandidate);
    const b8 scratchStateReady = context->renderer->m_shadowComputePersistentState.commit(*context->scratchStateCandidate);
    context->stateReady = returnStateReady && scratchStateReady;
    context->renderer->m_raytracingSystem.finalizeSoftShadowTemporalHistory(*context->targets);
    if(context->stateReady && context->lightSpacePrepared)
        context->renderer->m_raytracingSystem.acceptLightSpaceShadowCapture(context->lightSpaceCaptureTicket, *context->lightSpacePrepared);
    return context->stateReady;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 FrameExecuteLifecycle::PrepareSoftwareCausticsTask(void* const rawContext, const Core::CommandListResourceStateHandoff* const finalState){
    FrameExecuteLifecycle::SoftwareCausticsStateLifecycleContext* const context = static_cast<SoftwareCausticsStateLifecycleContext*>(rawContext);
    if(!context || context->irradianceTextures.empty() || context->scratchTextures.empty() || !finalState)
        return false;

    const b8 returnStateReady = context->renderer->m_causticIrradianceReturnState.buildFilteredResourceSubset(*finalState, context->irradianceTextures, {}, context->scratchArena, *context->returnStateCandidate);
    const b8 scratchStateReady = context->renderer->m_causticsComputePersistentState.buildFilteredResourceSubset(*finalState, context->scratchTextures, {}, context->scratchArena, *context->scratchStateCandidate);
    context->statePrepared = returnStateReady && scratchStateReady;
    return context->statePrepared;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 FrameExecuteLifecycle::AcceptSoftwareCausticsTask(void* const rawContext, [[maybe_unused]] const Core::QueueSubmissionToken& token)noexcept{
    FrameExecuteLifecycle::SoftwareCausticsStateLifecycleContext* const context = static_cast<SoftwareCausticsStateLifecycleContext*>(rawContext);
    if(!context || !context->statePrepared)
        return false;

    const b8 returnStateReady = context->renderer->m_causticIrradianceReturnState.commit(*context->returnStateCandidate);
    const b8 scratchStateReady = context->renderer->m_causticsComputePersistentState.commit(*context->scratchStateCandidate);
    context->stateReady = returnStateReady && scratchStateReady;
    return context->stateReady;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 FrameExecuteLifecycle::PrepareSurfelGiTask(void* const rawContext, const Core::CommandListResourceStateHandoff* const finalState){
    FrameExecuteLifecycle::SurfelGiStateLifecycleContext* const context = static_cast<SurfelGiStateLifecycleContext*>(rawContext);
    if(!context || context->returnTextures.empty() || context->counterBuffers.empty() || context->computeTextures.empty() || context->computeBuffers.empty() || !finalState)
        return false;

    const b8 returnStateReady = context->renderer->m_surfelIrradianceReturnState.buildFilteredResourceSubset(*finalState, context->returnTextures, {}, context->scratchArena, *context->returnStateCandidate);
    const b8 counterStateReady = context->renderer->m_surfelGiCounterPersistentState.buildFilteredResourceSubset(*finalState, {}, context->counterBuffers, context->scratchArena, *context->counterStateCandidate);
    const b8 computeStateReady =
        context->renderer->m_surfelGiComputePersistentState.buildFilteredResourceSubset(
            *finalState,
            context->computeTextures,
            context->computeBuffers,
            context->scratchArena,
            *context->computeStateCandidate
        )
    ;
    context->statePrepared = returnStateReady && counterStateReady && computeStateReady;
    return context->statePrepared;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 FrameExecuteLifecycle::AcceptSurfelGiTask(void* const rawContext, const Core::QueueSubmissionToken& token)noexcept{
    FrameExecuteLifecycle::SurfelGiStateLifecycleContext* const context = static_cast<SurfelGiStateLifecycleContext*>(rawContext);
    if(!context || !context->statePrepared)
        return false;

    RendererFramePipeline& renderer = *context->renderer;
    const b8 returnStateReady = renderer.m_surfelIrradianceReturnState.commit(*context->returnStateCandidate);
    const b8 counterStateReady = renderer.m_surfelGiCounterPersistentState.commit(*context->counterStateCandidate);
    const b8 computeStateReady = renderer.m_surfelGiComputePersistentState.commit(*context->computeStateCandidate);
    context->stateReady = returnStateReady && counterStateReady && computeStateReady;
    if(context->stateReady)
        renderer.m_raytracingSystem.confirmRayTraceContextReadSubmission(token);
    return context->stateReady;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 FrameExecuteLifecycle::PrepareHardwareCausticsTask(void* const rawContext, const Core::CommandListResourceStateHandoff* const finalState){
    FrameExecuteLifecycle::HardwareCausticsStateLifecycleContext* const context = static_cast<HardwareCausticsStateLifecycleContext*>(rawContext);
    if(!context || context->accumulatorTextures.empty() || !finalState)
        return false;

    const b8 accumulatorStateReady =
        context->renderer->m_hardwareCausticAccumulatorPersistentState.buildFilteredResourceSubset(*finalState, context->accumulatorTextures, {}, context->scratchArena, *context->accumulatorStateCandidate)
    ;
    context->statePrepared = accumulatorStateReady;
    return context->statePrepared;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 FrameExecuteLifecycle::AcceptHardwareCausticsTask(void* const rawContext, [[maybe_unused]] const Core::QueueSubmissionToken& token)noexcept{
    FrameExecuteLifecycle::HardwareCausticsStateLifecycleContext* const context = static_cast<HardwareCausticsStateLifecycleContext*>(rawContext);
    if(!context || !context->statePrepared)
        return false;

    const b8 accumulatorStateReady = context->renderer->m_hardwareCausticAccumulatorPersistentState.commit(*context->accumulatorStateCandidate);
    context->stateReady = accumulatorStateReady;
    return context->stateReady;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 FrameExecuteLifecycle::PrepareDeferredLightingTask(void* const rawContext, const Core::CommandListResourceStateHandoff* const finalState){
    FrameExecuteLifecycle::DeferredLightingStateLifecycleContext* const context = static_cast<DeferredLightingStateLifecycleContext*>(rawContext);
    if(!context || context->shadowReturnTextures.empty() || context->causticReturnTextures.empty() || context->surfelReturnTextures.empty() || !finalState)
        return false;

    b8 shadowStateReady = true;
    b8 causticStateReady = true;
    b8 surfelStateReady = true;
    if(!context->usesLaggedHistory){
        shadowStateReady = context->renderer->m_shadowVisibilityReturnState.buildFilteredResourceSubset(*finalState, context->shadowReturnTextures, {}, context->scratchArena, *context->shadowReturnStateCandidate);
        causticStateReady = context->renderer->m_causticIrradianceReturnState.buildFilteredResourceSubset(
            *finalState,
            context->causticReturnTextures,
            {},
            context->scratchArena,
            *context->causticReturnStateCandidate
        );
        surfelStateReady = context->renderer->m_surfelIrradianceReturnState.buildFilteredResourceSubset(*finalState, context->surfelReturnTextures, {}, context->scratchArena, *context->surfelReturnStateCandidate);
    }
    context->statePrepared = shadowStateReady && causticStateReady && surfelStateReady;
    return context->statePrepared;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 FrameExecuteLifecycle::AcceptDeferredLightingTask(void* const rawContext, [[maybe_unused]] const Core::QueueSubmissionToken& token){
    FrameExecuteLifecycle::DeferredLightingStateLifecycleContext* const context = static_cast<DeferredLightingStateLifecycleContext*>(rawContext);
    if(!context || !context->statePrepared)
        return false;

    RendererFramePipeline& renderer = *context->renderer;
    if(context->usesLaggedHistory)
        context->targets->laggedLightingHistory.slotsUploaded = true;
    b8 shadowStateReady = true;
    b8 causticStateReady = true;
    b8 surfelStateReady = true;
    if(!context->usesLaggedHistory){
        shadowStateReady = renderer.m_shadowVisibilityReturnState.commit(*context->shadowReturnStateCandidate);
        causticStateReady = renderer.m_causticIrradianceReturnState.commit(*context->causticReturnStateCandidate);
        surfelStateReady = renderer.m_surfelIrradianceReturnState.commit(*context->surfelReturnStateCandidate);
    }
    context->stateReady = shadowStateReady && causticStateReady && surfelStateReady;
    if(!context->stateReady)
        return false;
    if(context->usesLaggedHistory){
        renderer.reportLaggedLightingTransition(LaggedLightingReport::ActiveHistoryAccepted, context->targets->laggedLightingHistory.generation);
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

