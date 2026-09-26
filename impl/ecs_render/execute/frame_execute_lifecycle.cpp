// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/execute/frame_execute_lifecycle.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool FrameExecuteLifecycle::PrepareShadowPrepareTask(
    void* const rawContext,
    const Core::CommandListResourceStateHandoff* const finalState
){
    FrameExecuteLifecycle::ShadowPrepareStateLifecycleContext* const context =
        static_cast<ShadowPrepareStateLifecycleContext*>(rawContext)
    ;
    if(!context || !context->renderer || !context->stateCandidate || !finalState)
        return false;
    if(context->bufferCount != 0u && !context->buffers)
        return false;

    const bool candidateBuilt = context->renderer->m_shadowPreparePersistentState.buildMergedBufferSubset(
        *context->stateCandidate,
        *finalState,
        context->buffers,
        context->bufferCount,
        context->scratchArena
    );
    const bool candidatePresent = context->stateCandidate->valid() && !context->stateCandidate->empty();
    context->statePrepared = candidateBuilt && (candidatePresent || !context->stateCandidateRequired);
    return context->statePrepared;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool FrameExecuteLifecycle::AcceptShadowPrepareTask(
    void* const rawContext,
    const Core::QueueSubmissionToken& token
){
    FrameExecuteLifecycle::ShadowPrepareStateLifecycleContext* const context =
        static_cast<ShadowPrepareStateLifecycleContext*>(rawContext)
    ;
    if(
        !context
        || !context->frameTimingTransaction
        || !context->renderer
        || !context->stateCandidate
        || !context->statePrepared
    )
        return false;

    if(!context->frameTimingTransaction->confirmBeginSubmission(token)){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: failed to confirm accepted frame timing prefix; quarantining timing without rejecting native work"));
        context->frameTimingTransaction->discard();
    }

    RendererFramePipeline& renderer = *context->renderer;
    if(
        !context->stateCandidate->valid()
        || (context->stateCandidate->empty() && context->stateCandidateRequired)
    ){
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
    renderer.m_raytracingSystem.confirmAcceptedShadowPrepareAccelStructStateHandoffs();
    renderer.m_raytracingSystem.confirmPreparedMeshSwBvhBuilds();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool FrameExecuteLifecycle::PrepareShadowVisibilityTask(
    void* const rawContext,
    const Core::CommandListResourceStateHandoff* const finalState
){
    FrameExecuteLifecycle::ShadowVisibilityStateLifecycleContext* const context =
        static_cast<ShadowVisibilityStateLifecycleContext*>(rawContext)
    ;
    if(
        !context
        || !context->renderer
        || !context->returnStateCandidate
        || !context->scratchStateCandidate
        || !context->returnTextures
        || !context->scratchTextures
        || !context->scratchBuffers
        || !finalState
    )
        return false;

    // An inactive route retains its live allocation and accepted native state until that route runs again.
    const bool scratchStateReady = context->renderer->m_shadowComputePersistentState.buildMergedResourceSubset(
        *context->scratchStateCandidate,
        *finalState,
        context->scratchTextures,
        context->scratchTextureCount,
        context->scratchBuffers,
        context->scratchBufferCount,
        context->scratchArena
    );
    bool returnStateReady = true;
    if(context->runsOnCompute){
        returnStateReady = context->renderer->m_shadowVisibilityReturnState.buildFilteredResourceSubset(
            *context->returnStateCandidate,
            *finalState,
            context->returnTextures,
            context->returnTextureCount,
            nullptr,
            0u,
            context->scratchArena
        );
    }
    context->statePrepared = returnStateReady && scratchStateReady;
    return context->statePrepared;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool FrameExecuteLifecycle::AcceptShadowVisibilityTask(
    void* const rawContext,
    const Core::QueueSubmissionToken& token
){
    static_cast<void>(token);
    FrameExecuteLifecycle::ShadowVisibilityStateLifecycleContext* const context =
        static_cast<ShadowVisibilityStateLifecycleContext*>(rawContext)
    ;
    if(
        !context
        || !context->renderer
        || !context->targets
        || !context->returnStateCandidate
        || !context->scratchStateCandidate
        || !context->statePrepared
    )
        return false;

    bool returnStateReady = true;
    if(context->runsOnCompute)
        returnStateReady = context->renderer->m_shadowVisibilityReturnState.commit(*context->returnStateCandidate);
    const bool scratchStateReady =
        context->renderer->m_shadowComputePersistentState.commit(*context->scratchStateCandidate)
    ;
    context->stateReady = returnStateReady && scratchStateReady;
    context->renderer->m_raytracingSystem.finalizeSoftShadowTemporalHistory(*context->targets);
    if(context->stateReady && context->lightSpacePrepared)
        context->renderer->m_raytracingSystem.acceptLightSpaceShadowCapture(context->lightSpaceCaptureTicket, *context->lightSpacePrepared);
    return context->stateReady;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool FrameExecuteLifecycle::PrepareSoftwareCausticsTask(
    void* const rawContext,
    const Core::CommandListResourceStateHandoff* const finalState
){
    FrameExecuteLifecycle::SoftwareCausticsStateLifecycleContext* const context =
        static_cast<SoftwareCausticsStateLifecycleContext*>(rawContext)
    ;
    if(
        !context
        || !context->renderer
        || !context->returnStateCandidate
        || !context->scratchStateCandidate
        || !context->irradianceTextures
        || !context->scratchTextures
        || !finalState
    )
        return false;

    bool returnStateReady = true;
    if(context->runsOnCompute){
        returnStateReady = context->renderer->m_causticIrradianceReturnState.buildFilteredResourceSubset(
            *context->returnStateCandidate,
            *finalState,
            context->irradianceTextures,
            context->irradianceTextureCount,
            nullptr,
            0u,
            context->scratchArena
        );
    }
    const bool scratchStateReady = context->renderer->m_causticsComputePersistentState.buildFilteredResourceSubset(
        *context->scratchStateCandidate,
        *finalState,
        context->scratchTextures,
        context->scratchTextureCount,
        nullptr,
        0u,
        context->scratchArena
    );
    context->statePrepared = returnStateReady && scratchStateReady;
    return context->statePrepared;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool FrameExecuteLifecycle::AcceptSoftwareCausticsTask(
    void* const rawContext,
    const Core::QueueSubmissionToken& token
){
    static_cast<void>(token);
    FrameExecuteLifecycle::SoftwareCausticsStateLifecycleContext* const context =
        static_cast<SoftwareCausticsStateLifecycleContext*>(rawContext)
    ;
    if(
        !context
        || !context->renderer
        || !context->returnStateCandidate
        || !context->scratchStateCandidate
        || !context->statePrepared
    )
        return false;

    bool returnStateReady = true;
    if(context->runsOnCompute)
        returnStateReady = context->renderer->m_causticIrradianceReturnState.commit(*context->returnStateCandidate);
    const bool scratchStateReady =
        context->renderer->m_causticsComputePersistentState.commit(*context->scratchStateCandidate)
    ;
    context->stateReady = returnStateReady && scratchStateReady;
    return context->stateReady;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool FrameExecuteLifecycle::PrepareSurfelGiTask(
    void* const rawContext,
    const Core::CommandListResourceStateHandoff* const finalState
){
    FrameExecuteLifecycle::SurfelGiStateLifecycleContext* const context = static_cast<SurfelGiStateLifecycleContext*>(rawContext);
    if(
        !context
        || !context->renderer
        || !context->returnStateCandidate
        || !context->counterStateCandidate
        || !context->computeStateCandidate
        || !context->returnTextures
        || !context->counterBuffers
        || !context->computeTextures
        || !context->computeBuffers
        || !finalState
    )
        return false;

    const bool returnStateReady = context->renderer->m_surfelIrradianceReturnState.buildFilteredResourceSubset(
        *context->returnStateCandidate,
        *finalState,
        context->returnTextures,
        context->returnTextureCount,
        nullptr,
        0u,
        context->scratchArena
    );
    const bool counterStateReady = context->renderer->m_surfelGiCounterPersistentState.buildFilteredResourceSubset(
        *context->counterStateCandidate,
        *finalState,
        nullptr,
        0u,
        context->counterBuffers,
        context->counterBufferCount,
        context->scratchArena
    );
    const bool computeStateReady =
        context->renderer->m_surfelGiComputePersistentState.buildFilteredResourceSubset(
            *context->computeStateCandidate,
            *finalState,
            context->computeTextures,
            context->computeTextureCount,
            context->computeBuffers,
            context->computeBufferCount,
            context->scratchArena
        )
    ;
    context->statePrepared = returnStateReady && counterStateReady && computeStateReady;
    return context->statePrepared;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool FrameExecuteLifecycle::AcceptSurfelGiTask(
    void* const rawContext,
    const Core::QueueSubmissionToken& token
){
    static_cast<void>(token);
    FrameExecuteLifecycle::SurfelGiStateLifecycleContext* const context = static_cast<SurfelGiStateLifecycleContext*>(rawContext);
    if(
        !context
        || !context->renderer
        || !context->returnStateCandidate
        || !context->counterStateCandidate
        || !context->computeStateCandidate
        || !context->statePrepared
    )
        return false;

    RendererFramePipeline& renderer = *context->renderer;
    const bool returnStateReady = renderer.m_surfelIrradianceReturnState.commit(*context->returnStateCandidate);
    const bool counterStateReady = renderer.m_surfelGiCounterPersistentState.commit(*context->counterStateCandidate);
    const bool computeStateReady = renderer.m_surfelGiComputePersistentState.commit(*context->computeStateCandidate);
    context->stateReady = returnStateReady && counterStateReady && computeStateReady;
    return context->stateReady;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool FrameExecuteLifecycle::PrepareHardwareCausticsTask(
    void* const rawContext,
    const Core::CommandListResourceStateHandoff* const finalState
){
    FrameExecuteLifecycle::HardwareCausticsStateLifecycleContext* const context =
        static_cast<HardwareCausticsStateLifecycleContext*>(rawContext)
    ;
    if(
        !context
        || !context->renderer
        || !context->accumulatorStateCandidate
        || !context->accumulatorTextures
        || !finalState
    )
        return false;

    const bool accumulatorStateReady =
        context->renderer->m_hardwareCausticAccumulatorPersistentState.buildFilteredResourceSubset(
            *context->accumulatorStateCandidate,
            *finalState,
            context->accumulatorTextures,
            context->accumulatorTextureCount,
            nullptr,
            0u,
            context->scratchArena
        )
    ;
    context->statePrepared = accumulatorStateReady;
    return context->statePrepared;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool FrameExecuteLifecycle::AcceptHardwareCausticsTask(
    void* const rawContext,
    const Core::QueueSubmissionToken& token
){
    static_cast<void>(token);
    FrameExecuteLifecycle::HardwareCausticsStateLifecycleContext* const context =
        static_cast<HardwareCausticsStateLifecycleContext*>(rawContext)
    ;
    if(
        !context
        || !context->renderer
        || !context->accumulatorStateCandidate
        || !context->statePrepared
    )
        return false;

    const bool accumulatorStateReady =
        context->renderer->m_hardwareCausticAccumulatorPersistentState.commit(*context->accumulatorStateCandidate)
    ;
    context->stateReady = accumulatorStateReady;
    return context->stateReady;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool FrameExecuteLifecycle::PrepareDeferredLightingTask(
    void* const rawContext,
    const Core::CommandListResourceStateHandoff* const finalState
){
    FrameExecuteLifecycle::DeferredLightingStateLifecycleContext* const context =
        static_cast<DeferredLightingStateLifecycleContext*>(rawContext)
    ;
    if(
        !context
        || !context->renderer
        || !context->shadowReturnStateCandidate
        || !context->causticReturnStateCandidate
        || !context->surfelReturnStateCandidate
        || !context->shadowReturnTextures
        || !context->causticReturnTextures
        || !context->surfelReturnTextures
        || !finalState
    )
        return false;

    bool shadowStateReady = true;
    bool causticStateReady = true;
    bool surfelStateReady = true;
    if(context->runsOnCompute && !context->usesLaggedHistory){
        shadowStateReady = context->renderer->m_shadowVisibilityReturnState.buildFilteredResourceSubset(
            *context->shadowReturnStateCandidate,
            *finalState,
            context->shadowReturnTextures,
            context->shadowReturnTextureCount,
            nullptr,
            0u,
            context->scratchArena
        );
        causticStateReady = context->renderer->m_causticIrradianceReturnState.buildFilteredResourceSubset(
            *context->causticReturnStateCandidate,
            *finalState,
            context->causticReturnTextures,
            context->causticReturnTextureCount,
            nullptr,
            0u,
            context->scratchArena
        );
        surfelStateReady = context->renderer->m_surfelIrradianceReturnState.buildFilteredResourceSubset(
            *context->surfelReturnStateCandidate,
            *finalState,
            context->surfelReturnTextures,
            context->surfelReturnTextureCount,
            nullptr,
            0u,
            context->scratchArena
        );
    }
    context->statePrepared = shadowStateReady && causticStateReady && surfelStateReady;
    return context->statePrepared;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool FrameExecuteLifecycle::AcceptDeferredLightingTask(
    void* const rawContext,
    const Core::QueueSubmissionToken& token
){
    static_cast<void>(token);
    FrameExecuteLifecycle::DeferredLightingStateLifecycleContext* const context =
        static_cast<DeferredLightingStateLifecycleContext*>(rawContext)
    ;
    if(
        !context
        || !context->renderer
        || !context->targets
        || !context->shadowReturnStateCandidate
        || !context->causticReturnStateCandidate
        || !context->surfelReturnStateCandidate
        || !context->statePrepared
    )
        return false;

    RendererFramePipeline& renderer = *context->renderer;
    if(context->usesLaggedHistory)
        context->targets->laggedLightingHistory.slotsUploaded = true;
    bool shadowStateReady = true;
    bool causticStateReady = true;
    bool surfelStateReady = true;
    if(context->runsOnCompute && !context->usesLaggedHistory){
        shadowStateReady = renderer.m_shadowVisibilityReturnState.commit(*context->shadowReturnStateCandidate);
        causticStateReady = renderer.m_causticIrradianceReturnState.commit(*context->causticReturnStateCandidate);
        surfelStateReady = renderer.m_surfelIrradianceReturnState.commit(*context->surfelReturnStateCandidate);
    }
    context->stateReady = shadowStateReady && causticStateReady && surfelStateReady;
    if(!context->stateReady)
        return false;
    if(context->usesLaggedHistory){
        renderer.reportLaggedLightingTransition(
            RendererFramePipeline::LaggedLightingReport::ActiveHistoryAccepted,
            context->targets->laggedLightingHistory.generation
        );
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

