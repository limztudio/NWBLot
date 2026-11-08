// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/execute/frame_diagnostics.adhd.h>

#include <impl/ecs_render/renderer_frame_pipeline.h>
#include <impl/ecs_render/deferred/deferred_frame_target_builder.h>
#include <impl/ecs_render/shared/frame_types.adhd.h>
#include <impl/ecs_render/kernel/timing_names.h>
#include <impl/ecs_render/kernel/renderer_gpu_timing_scope_reservations.h>

#include <core/task/gpu/output_layer_contributor.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererFramePipeline::validateResources(const u32 width, const u32 height, const u32){
    m_raytracingSystem.logCapabilityOnce();
    if(width == 0 || height == 0)
        return true;

    if(!m_activeSceneSubsystems){
        if(!m_defaultSceneSubsystems.loadProjectStartScene(m_assetManager))
            return false;
        m_activeSceneSubsystems = &m_defaultSceneSubsystems;
        m_shaderSystem.setActiveSceneSubsystems(m_activeSceneSubsystems);
    }
    if(!m_activeSceneSubsystems->hasSubsystem<RendererSubsystem>()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: active scene does not include required subsystem '{}'"), ConvertString(RendererSubsystem::s_AssetTypeText));
        return false;
    }

    const u32 surfelResolveFactor = static_cast<u32>(m_surfelGiQualitySettings.resolveResolution);
    const u32 shadowReceiverFactor = m_raytracingSystem.requestedShadowReceiverFactor();
    if((m_frameTargets.surfelResolveFactor != 0u && m_frameTargets.surfelResolveFactor != surfelResolveFactor) || (m_frameTargets.shadowReceiverFactor != 0u && m_frameTargets.shadowReceiverFactor != shadowReceiverFactor)){
        // Shared producers must drain and recreate before their retained queue handoffs are cleared.
        if(!m_graphics.waitForIdle())
            return false;
        invalidateResources();
    }

    if(!RendererGpuTimingScopeReservations::Prepare(m_graphics))
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: GPU timing scope preparation failed; timing samples may be skipped"));

    DeferredFrameTargets* deferredTargets = m_frameTargets.valid() ? &m_frameTargets : nullptr;
    b8 targetsReady = deferredTargets && deferredTargets->width == width && deferredTargets->height == height && deferredTargets->surfelResolveFactor == surfelResolveFactor && deferredTargets->shadowReceiverFactor == shadowReceiverFactor;
    if(!targetsReady){
        // New targets invalidate stale compute scratch and visibility returns.
        resetTargetGenerationStateHandoffs();
        resetLaggedLightingHistoryTracking();
        resetFrameTargets();
        m_materialSystem.invalidateRendererPipelines();

        DeferredFrameTargetBuilder targetBuilder(m_graphics, m_rayTracingState, m_deferredSystem, m_avboitSystem, m_csgSystem, m_raytracingSystem);
        auto createdTargetsResult = targetBuilder.build(width, height, shadowReceiverFactor, surfelResolveFactor);
        if(!createdTargetsResult)
            return false;
        commitFrameTargets(Move(*createdTargetsResult));
        deferredTargets = &m_frameTargets;
        targetsReady = m_deferredSystem.createDeferredLightingPipeline() && m_deferredSystem.createDeferredCompositePipeline();
        if(!targetsReady)
            resetFrameTargets();
    }
    if(!targetsReady || !deferredTargets)
        return false;

    // Pipeline compatibility setup uses the stable framebuffer-zero prototype, not a currently acquired image.
    if(Core::Framebuffer* presentationFramebuffer = m_graphics.getFramebuffer(0u)){
        if(!m_deferredSystem.createDeferredPresentPipeline(*presentationFramebuffer))
            return false;
    }

    if(!m_reflectionSystem.prepareResources(width, height, m_graphics.queryFeatureSupport(Core::Feature::RayQuery), false, m_reflectionSettings))
        return false;

    if(!m_materialSystem.prepareMaterialPassBindingLayout())
        return false;

    if(isTransparentFeatureEnabled() && !m_avboitSystem.createAvboitPipelines())
        return false;

    if(!m_meshSystem.createMeshViewBuffer())
        return false;

    if(isCsgFeatureEnabled()){
        if(!m_csgSystem.createCsgIntervalPeelResources(*deferredTargets, true))
            return false;
    }
    else{
        if(!m_csgSystem.createCsgClipResources())
            return false;
    }

    return true;
}

void RendererFramePipeline::invalidateResources(){
    m_opticalVolumes.reset();
    m_deferredTaskTimingFeedback.reset();
    m_deferredLightingTaskGraphQueueAssignmentTelemetry.reset();
    m_preparedCsgFrameState = CsgFrameState{};
    m_preparedCsgFrameStateValid = false;
    m_preparedHasTransparentRenderers = false;
    m_preparedRefractionActive = false;
    m_preparedRefractionResources = RayTracingRefractionGraphResources{};
    m_shadowPrepareGraph.m_shadowPreparationOutcome.resourcesValid = false;
    m_shadowPrepareGraph.m_shadowPreparationOutcome.ready = false;
    m_shadowPrepareGraph.resetShadowPrepareTaskState();
    m_preparedTaskGraphPresentationContributor = nullptr;
    m_preparedTaskGraphOutputLayerContributor = nullptr;
    resetFrameTaskState();
    NWB_FATAL_ASSERT_MSG(m_deferredLightingRecordedGraph.releaseCommandListCache(), "renderer resource invalidation requires idle command-list caches");
    resetInvalidatedResourceStateHandoffs();
    resetLaggedLightingHistoryTracking();
    m_frameRenderRecoveryFailed = false;
    m_reflectionSystem.invalidateResources();
    m_raytracingSystem.invalidateResources();
    resetFrameTargets();
    // AVBOIT pipelines also consume Material's shared push layout, so release those pipelines before Material clears the layout owner.
    m_avboitSystem.invalidateResources();
    m_shaderSystem.invalidateResources();
    m_meshSystem.invalidateResources();
    m_materialSystem.invalidateResources();
    m_csgSystem.invalidateResources();
    m_deferredSystem.invalidateResources();
}

void RendererFramePipeline::update([[maybe_unused]] Core::ECS::World& world, [[maybe_unused]] f32 delta)noexcept{}

b8 RendererFramePipeline::prepareResources(Core::Framebuffer* framebuffer){
    m_shadowPrepareGraph.m_shadowPreparationOutcome.resourcesValid = false;
    m_shadowPrepareGraph.m_shadowPreparationOutcome.ready = false;
    m_preparedHasTransparentRenderers = false;
    m_preparedRefractionActive = false;
    m_preparedRefractionResources = RayTracingRefractionGraphResources{};
    m_preparedTaskGraphPresentationContributor = nullptr;
    m_preparedTaskGraphOutputLayerContributor = nullptr;

    if(!framebuffer)
        return false;

    const Core::AcquiredPresentationFrame& presentationFrame = m_graphics.acquiredPresentationFrame();
    const Core::FramebufferDesc& presentationFramebufferDesc = framebuffer->getDescription();
    if(!presentationFrame.valid() || presentationFrame.framebuffer.get() != framebuffer || presentationFramebufferDesc.colorAttachments.size() != 1u || presentationFramebufferDesc.colorAttachments[0].texture != presentationFrame.backBuffer.texture.get()){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: presentation preparation did not match the acquired frame; requesting recreation"));
        m_graphics.requestDeviceRecreation();
        return false;
    }

    if(m_frameTargets.surfelResolveFactor != static_cast<u32>(m_surfelGiQualitySettings.resolveResolution) || m_frameTargets.shadowReceiverFactor != m_raytracingSystem.requestedShadowReceiverFactor()){
        const Core::TextureDesc& presentationDesc = presentationFrame.backBuffer.texture->getDescription();
        if(!validateResources(presentationDesc.width, presentationDesc.height, presentationDesc.sampleCount))
            return false;
    }

    Core::Alloc::ScratchArena scratchArena(s_PrepareArena);
    m_meshSystem.pruneRuntimeMeshResources(scratchArena);
    const b8 hasVisibleTransparentRenderers = m_materialSystem.prepareVisibleMaterialSurfaceInfos();
    m_preparedHasTransparentRenderers = isTransparentFeatureEnabled() && hasVisibleTransparentRenderers;
    m_materialSystem.prepareVisibleMaterialInstanceMutableCache();
    m_preparedCsgFrameState = CsgFrameState{};
    m_preparedCsgFrameStateValid = false;

    if(!m_frameTargets.valid())
        return true;
    DeferredFrameTargets& deferredTargets = m_frameTargets;

    m_reflectionSystem.pollStatistics();
    m_raytracingSystem.refraction().pollRefractionStatistics();
    if(m_preparedHasTransparentRenderers)
        m_opticalVolumes.prepare(m_world, m_materialSystem, scratchArena);
    else
        m_opticalVolumes.reset();
    m_preparedCsgFrameState = (isCsgFeatureEnabled() && HasCsgFrameCandidates(m_world)) ? m_csgSystem.buildFrameState(scratchArena, m_materialSystem) : CsgFrameState{};
    m_preparedCsgFrameStateValid = true;
    const b8 hasCsgFrameWork = !m_preparedCsgFrameState.empty();
    if(!m_reflectionSystem.prepareResources(deferredTargets.width, deferredTargets.height, m_graphics.queryFeatureSupport(Core::Feature::RayQuery), hasCsgFrameWork && m_graphics.queryFeatureSupport(Core::Feature::RayQuery), m_reflectionSettings))
        return false;

    if(hasCsgFrameWork && !deferredTargets.csgIntervalTargetsValid())
        return false;

    // CSG receiver ranges are indexed by material-pass instance; an active pass reserves one range per renderer.
    const usize csgReceiverRangeCount = hasCsgFrameWork ? m_world.view<RendererComponent>().candidateCount() : 0u;

    if(!m_materialSystem.prepareMaterialPassResources(scratchArena, deferredTargets.framebuffer.get(), MaterialPipelinePass::Opaque, false, m_preparedCsgFrameState, nullptr))
        return false;

    if(m_preparedHasTransparentRenderers && !m_avboitSystem.prepareAvboitPassResources(scratchArena, m_preparedCsgFrameState, deferredTargets))
        return false;

    // Material owns growth. Once every domain pass prepares, publish one Mesh generation; retain the prior one on refresh.
    const ECSRenderDetail::MaterialPassBufferSnapshot materialBuffers = m_materialSystem.materialPassBufferSnapshot();
    if(materialBuffers.valid() && !m_meshSystem.prepareMeshFrameBindings(materialBuffers))
        return false;
    if(hasCsgFrameWork && !materialBuffers.valid())
        return false;

    // Create CSG resources once here; draw paths only consume prepared layouts and handles.
    if(hasCsgFrameWork && !m_csgSystem.prepareCsgFrameResources(csgReceiverRangeCount, static_cast<usize>(m_preparedCsgFrameState.cutterCount)))
        return false;

    // Resource selection and capacity growth happen before shared-graph compilation.
    // The first deferred packet records the corresponding GPU work later, after every selected handle has been imported declaratively.
    m_raytracingSystem.discardSurfelResourceInitialization();
    if(!m_raytracingSystem.preflightShadowVisibilityResources(deferredTargets, scratchArena)){
        m_shadowPrepareGraph.m_shadowPreparationOutcome.resourcesValid = false;
        m_shadowPrepareGraph.m_shadowPreparationOutcome.ready = false;
        m_raytracingSystem.discardPreflightShadowVisibilityResources();
        return false;
    }
    m_shadowPrepareGraph.m_shadowPreparationOutcome.resourcesValid = true;
    // The ray scene prepares geometry on its own. Reflection borrows neutral context only after that preflight picks a generation.
    const b8 reflectionHardwareRequested = m_reflectionSettings.traceMode == ReflectionTraceMode::Hardware || m_reflectionSettings.traceMode == ReflectionTraceMode::Hybrid;
    m_preparedReflectionSceneAvailable = reflectionHardwareRequested && m_raytracingSystem.prepareSceneQueryResources();
    ++m_reflectionFrameIndex;

    // Refraction loads shaders and creates pipelines in preparation; the graph build only consumes the snapshot.
    const b8 opticalCaptureRequested = m_preparedHasTransparentRenderers && (m_refractionEnabled || m_reflectionSettings.traceMode != ReflectionTraceMode::Disabled);
    m_preparedRefractionActive = opticalCaptureRequested && m_raytracingSystem.refraction().prepareRefractionResources();
    const b8 hasTransparentCsgFrameWork = m_preparedCsgFrameState.hasTransparentStaticWork || m_preparedCsgFrameState.hasTransparentSkinnedWork;
    if(m_preparedRefractionActive && hasTransparentCsgFrameWork && !m_csgSystem.prepareRefractionIntervalCapResources(deferredTargets))
        return false;
    m_preparedRefractionResources = m_raytracingSystem.refraction().snapshotRefractionGraphResources(deferredTargets.width, deferredTargets.height, m_reflectionFrameIndex);
    m_preparedRefractionResources.refractionEnabled = m_refractionEnabled;
    if(!m_refractionEnabled){
        m_preparedRefractionResources.pipeline = m_preparedRefractionResources.screenFallbackPipeline;
        m_preparedRefractionResources.densePipeline = m_preparedRefractionResources.denseScreenFallbackPipeline;
        m_preparedRefractionResources.usesHardwareTrace = false;
        m_preparedRefractionResources.tlasHeapHandle = Core::GpuDescriptorHandle::Invalid();
    }

    if(Core::IGpuTaskGraphOutputLayerContributor* const contributor = m_graphics.taskGraphOutputLayerContributor()){
        if(contributor->prepareTaskGraphOutputLayer(presentationFrame))
            m_preparedTaskGraphOutputLayerContributor = contributor;
        else if(m_graphics.isDeviceRecreationRequested()){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: output layer requested recreation during preparation"));
            return false;
        }
        else
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: output layer preparation failed; rendering scene output without its layer"));
    }

    if(Core::IGpuTaskGraphPresentationContributor* const contributor = m_graphics.taskGraphPresentationContributor()){
        if(contributor->prepareTaskGraphPresentation(presentationFrame))
            m_preparedTaskGraphPresentationContributor = contributor;
        else if(m_graphics.isDeviceRecreationRequested()){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: presentation contributor requested recreation during preparation"));
            return false;
        }
        else
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: presentation contributor preparation failed; rendering scene output without its overlay"));
    }

    return true;
}



void RendererFramePipeline::commitFrameTargets(DeferredFrameTargets&& targets){
    m_frameTargets = Move(targets);

    DeferredFrameDiagnostics{}.logCommittedDeferredFrameTargets(
        m_frameTargets.width,
        m_frameTargets.height,
        Core::GetFormatInfo(m_frameTargets.albedoFormat).getName(),
        Core::GetFormatInfo(m_frameTargets.normalFormat).getName(),
        Core::GetFormatInfo(m_frameTargets.worldPositionFormat).getName(),
        Core::GetFormatInfo(m_frameTargets.opaqueColorFormat).getName(),
        Core::GetFormatInfo(m_frameTargets.compositeColorFormat).getName(),
        Core::GetFormatInfo(m_frameTargets.depthFormat).getName(),
        Core::GetFormatInfo(m_frameTargets.shadowVisibilityFormat).getName(),
        m_frameTargets.csgPeelLayerCount,
        Core::GetFormatInfo(m_frameTargets.csgCapNormalFormat).getName(),
        Core::GetFormatInfo(m_frameTargets.csgIntervalDepthFormat).getName(),
        Core::GetFormatInfo(m_frameTargets.csgIntervalIdFormat).getName(),
        m_frameTargets.csgReceiverEventLayerCount,
        Core::GetFormatInfo(m_frameTargets.csgReceiverEventDataFormat).getName(),
        Core::GetFormatInfo(m_frameTargets.csgReceiverEventCountFormat).getName(),
        m_frameTargets.csgReceiverSpanLayerCount,
        Core::GetFormatInfo(m_frameTargets.csgReceiverSpanDataFormat).getName(),
        Core::GetFormatInfo(m_frameTargets.csgReceiverSpanCountFormat).getName(),
        m_frameTargets.csgRemovedIntervalLayerCount,
        Core::GetFormatInfo(m_frameTargets.csgRemovedIntervalDepthFormat).getName(),
        Core::GetFormatInfo(m_frameTargets.csgRemovedIntervalCapNormalFormat).getName(),
        Core::GetFormatInfo(m_frameTargets.csgRemovedIntervalDataFormat).getName(),
        Core::GetFormatInfo(m_frameTargets.csgRemovedIntervalCountFormat).getName(),
        Core::GetFormatInfo(m_frameTargets.avboit.accumColorFormat).getName(),
        Core::GetFormatInfo(m_frameTargets.avboit.accumExtinctionFormat).getName(),
        Core::GetFormatInfo(m_frameTargets.avboit.transmittanceFormat).getName()
    );
}

void RendererFramePipeline::resetFrameTargets(){
    // AVBOIT owns descriptor registrations embedded in the aggregate, so retire them before Deferred clears it.
    m_avboitSystem.resetAvboitFrameTargets(m_frameTargets.avboit);
    m_deferredSystem.resetDeferredFrameTargets(m_frameTargets);
}

void RendererFramePipeline::resetDeferredTaskGraphRuntime(){
    {
        Core::GpuCompiledGraph::ReadView planAccess(m_deferredLightingCompiledGraph);
        if(planAccess.valid()){
            const u64 recordingAttemptGeneration = m_deferredLightingRecordedGraph.recordingAttemptGeneration();
            if(recordingAttemptGeneration != 0u && !m_deferredLightingSubmissionTransaction.discardUnaccepted(m_deferredLightingTaskGraph, m_deferredLightingCompiledGraph, recordingAttemptGeneration))
                NWB_FATAL_ASSERT_MSG(false, "renderer task-graph reset must resolve every unaccepted packet");
            m_deferredLightingSubmissionTransaction.reset(m_deferredLightingCompiledGraph);
            m_deferredLightingRecordedGraph.reset(m_deferredLightingCompiledGraph);
        }
    }

    m_deferredLightingTaskGraph.reset();
    m_deferredFrameTimingBeginTask = {};
    m_deferredLightingTaskGraphAnalysis.reset();
    m_deferredLightingTaskGraphQueueAssignments.reset();
    m_deferredLightingCompiledGraph.reset();
    m_deferredLightingTaskGraphDeclarationSeconds = 0.0;
    m_deferredLightingTaskGraphDeclared = false;
    m_deferredLightingTaskGraphScheduled = false;
}

void RendererFramePipeline::resetFrameTaskState(){
    resetDeferredFrameTaskIdsAndFlags();
    resetDeferredTaskGraphRuntime();
}

void RendererFramePipeline::resetDeferredFrameTaskIdsAndFlags()noexcept{
    m_shadowPrepareGraph.resetShadowPrepareUploadTaskState();
    m_deferredLaggedLightingHistorySlotsUploadTask = {};
    m_graphicsPrefixGraph.m_graphicsPrefixMeshViewSetupTask = {};
    m_graphicsPrefixGraph.m_graphicsPrefixSceneShadingSetupTask = {};
    m_graphicsPrefixGraph.m_graphicsPrefixDeferredClearTask = {};
    m_graphicsPrefixGraph.resetGraphicsPrefixTaskState();
    m_graphicsPrefixGraph.m_graphicsPrefixMeshViewSetupReady = false;
    m_graphicsPrefixGraph.m_graphicsPrefixSceneShadingSetupReady = false;
    resetSharedDeferredFrameTaskState();
    m_deferredFrameRecoveryArmed = false;
    m_deferredFrameRecoveryRetiresTiming = false;
    m_deferredPresentationOverlayRequired = false;
}

void RendererFramePipeline::resetSharedDeferredFrameTaskState()noexcept{
    m_shadowVisibilityGraph.m_deferredShadowVisibilityOpaqueTask = {};
    m_shadowVisibilityGraph.m_deferredShadowVisibilityOpaqueFirstWaveletTask = {};
    m_shadowVisibilityGraph.m_deferredShadowVisibilityOpaqueResolveTask = {};
    m_shadowVisibilityGraph.m_deferredShadowVisibilityTransparentTraceTask = {};
    m_shadowVisibilityGraph.m_deferredShadowVisibilityTransparentTemporalMergeTask = {};
    m_shadowVisibilityGraph.m_deferredShadowVisibilityTransparentFirstWaveletTask = {};
    m_shadowVisibilityGraph.m_deferredShadowVisibilityDiagnosticsTask = {};
    m_shadowVisibilityGraph.m_deferredShadowVisibilityAdaptiveCounterClearTask = {};
    m_shadowVisibilityGraph.m_deferredShadowVisibilityAllLitClearTask = {};
    m_shadowVisibilityGraph.m_deferredShadowVisibilityTask = {};
    m_causticsGraph.resetDeferredCausticsTaskState();
    m_surfelGiGraph.m_deferredSurfelGiPreparationTask = {};
    m_surfelGiGraph.m_deferredSurfelGiInitializationLifecycleTask = {};
    m_surfelGiGraph.m_deferredSurfelGiSnapshotCopyTask = {};
    m_surfelGiGraph.m_deferredSurfelGiIrradianceClearTask = {};
    m_surfelGiGraph.m_deferredSurfelGiAgeFreeTask = {};
    m_surfelGiGraph.m_deferredSurfelGiCellHeadClearTask = {};
    m_surfelGiGraph.m_deferredSurfelGiHashBuildTask = {};
    m_surfelGiGraph.m_deferredSurfelGiSpawnTask = {};
    m_surfelGiGraph.m_deferredSurfelGiTraceBuildArgsTask = {};
    m_surfelGiGraph.m_deferredSurfelGiTraceTask = {};
    m_surfelGiGraph.m_deferredSurfelGiResolveTask = {};
    m_surfelGiGraph.m_deferredSurfelGiTask = {};
    m_surfelGiGraph.m_deferredSurfelGiCounterReadbackTask = {};
    m_causticsGraph.m_deferredHardwareCausticsTask = {};
    m_avboitSystem.resetTaskGraphStage();
    m_deferredLightingTask = {};
    m_deferredCompositeTask = {};
    m_deferredPresentationOverlayTask = {};
    m_deferredPresentTask = {};
    m_deferredFrameTimingEndTask = {};
    m_deferredLaggedLightingHistoryTask = {};
    m_deferredFrameRecoveryTask = {};
    m_deferredSurfelGiCounterReadbackCompletion = {};
    m_deferredLightingHistoryReadReadyCompletion = {};
    m_deferredLightingHistoryWriterDrainCompletion = {};
}




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

