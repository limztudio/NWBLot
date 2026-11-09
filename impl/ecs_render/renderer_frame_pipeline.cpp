// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/execute/frame_diagnostics.adhd.h>
#include "renderer_frame_pipeline.h"
#include <impl/ecs_render/shared/frame_types.adhd.h>
#include <impl/ecs_render/kernel/timing_names.h>
#include <impl/assets_graphics/renderer_subsystem.adhd.h>
#include <impl/ecs_render/deferred/deferred_frame_target_builder.h>
#include <impl/ecs_render/kernel/renderer_gpu_timing_scope_reservations.h>
#include <core/task/gpu/output_layer_contributor.h>
#include <impl/ecs_render/execute/deferred_frame_topology_validator.h>
#include <impl/ecs_render/execute/frame_execute_lifecycle.h>
#include <impl/ecs_render/execute/frame_execute_present_tails.h>
#include <impl/ecs_render/execute/frame_execute_recovery.h>
#include <impl/ecs_render/execute/frame_execute_state_lifecycles.h>
#include <impl/ecs_render/execute/frame_execute_timing_state.h>
#include <impl/ecs_render/kernel/task_graph_clear_timing.h>
#include <core/graphics/gpu_timing.h>
#include <core/task/gpu/scheduler.h>
#include <impl/ecs_render/kernel/frame_graph_runtime_statistics.h>
#include <impl/ecs_render/kernel/renderer_device_diagnostics_label.h>
#include <impl/ecs_render/kernel/renderer_graph_runtime_label.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/frame_graph_nodes.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_renderer_frame_pipeline{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr Array<Name, 2> s_DeferredTaskTimingFeedbackScopes = { RendererGpuTimingScope::s_AvboitDepthWarp.identity, RendererGpuTimingScope::s_AvboitIntegration.identity, };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RendererFramePipeline::RendererFramePipeline(Core::Alloc::GlobalArena& arena, Core::ECS::World& world, Core::GraphicsRuntime& graphics, Core::Assets::AssetManager& assetManager, RendererShaderPathResolveCallback shaderPathResolver)
    : m_arena(arena)
    , m_world(world)
    , m_graphics(graphics)
    , m_assetManager(assetManager)
    , m_shaderPathResolver(Move(shaderPathResolver))
    , m_csgShapeRegistry(arena)
    , m_frameGraphRendererLabel(arena)
    , m_defaultSceneSubsystems(arena)
    , m_meshState(arena)
    , m_materialState(arena)
    , m_opticalVolumes(arena)
    , m_rayTracingState(arena)
    , m_shadowComputePersistentState(arena)
    , m_shadowVisibilityReturnState(arena)
    , m_shadowPreparePersistentState(arena)
    , m_causticsComputePersistentState(arena)
    , m_hardwareCausticAccumulatorPersistentState(arena)
    , m_causticIrradianceReturnState(arena)
    , m_surfelGiComputePersistentState(arena)
    , m_surfelGiCounterPersistentState(arena)
    , m_surfelIrradianceReturnState(arena)
    , m_shaderSystem(graphics, assetManager, m_shaderPathResolver)
    , m_meshSystem(arena, world, graphics, assetManager, m_meshState)
    , m_csgSystem(arena, world, graphics, m_csgShapeRegistry, m_csgState, m_shaderSystem, m_meshSystem)
    , m_materialSystem(arena, world, graphics, assetManager, m_csgShapeRegistry, m_materialState, m_shaderSystem, m_meshSystem, m_csgSystem, m_opticalVolumes)
    , m_deferredSystem(arena, world, graphics, m_deferredState, m_shaderSystem)
    , m_reflectionSystem(arena, graphics, m_shaderSystem)
    , m_avboitSystem(arena, graphics, m_avboitState, m_shaderSystem, m_materialSystem, m_csgSystem)
    , m_raytracingSystem(arena, world, graphics, m_shaderSystem, m_meshSystem, m_materialSystem, m_rayTracingState, m_opticalVolumes, m_csgShapeRegistry)
    , m_deferredTaskTimingFeedback(arena, graphics, __hidden_renderer_frame_pipeline::s_DeferredTaskTimingFeedbackScopes)
    , m_deferredLightingTaskGraph(arena)
    , m_deferredLightingTaskGraphAnalysis(arena)
    , m_deferredLightingTaskGraphQueueAssignments(arena)
    , m_deferredLightingTaskGraphQueueAssignmentTelemetry(arena)
    , m_deferredLightingCompiledGraph(arena)
    , m_deferredLightingRecordedGraph(arena)
    , m_deferredLightingSubmissionTransaction(arena)
{
    if(!RegisterBuiltInCsgShapeTypes(m_csgShapeRegistry))
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register built-in CSG shape types"));

    m_deferredTaskTimingFeedback.activate();
}

RendererFramePipeline::~RendererFramePipeline(){
    // Resolve payloads eagerly while owners are alive.
    m_deferredLightingTaskGraph.reset();
    m_deferredTaskTimingFeedback.deactivate();
}

void RendererFramePipeline::setActiveSceneSubsystems(const ActiveSceneSubsystems* subsystems)noexcept{
    m_activeSceneSubsystems = subsystems;
    m_shaderSystem.setActiveSceneSubsystems(subsystems);
}

b8 RendererFramePipeline::setSurfelGiQualitySettings(const SurfelGiQualitySettings& settings)noexcept{
    if(!ValidateSurfelGiQualitySettings(settings))
        return false;
    m_surfelGiQualitySettings = settings;
    return true;
}

b8 RendererFramePipeline::setReflectionSettings(const ReflectionSettings& settings)noexcept{
    if(!ValidateReflectionSettings(settings))
        return false;
    m_reflectionSettings = settings;
    return true;
}

void RendererFramePipeline::setFrameLaggedAsyncLightingEnabled(const b8 enabled)noexcept{
    if(m_frameLaggedAsyncLightingEnabled == enabled)
        return;
    // Proof that disabling the mode returned to the current-frame path: set when turning off, cleared by the next accepted normal frame.
    m_laggedLightingCurrentFrameAcceptancePending = m_frameLaggedAsyncLightingEnabled && !enabled;
    if(m_frameLaggedAsyncLightingEnabled && !enabled && m_laggedLightingHistorySubmissionToken.valid()){
        m_laggedLightingHistoryWriterDrainToken = m_laggedLightingHistorySubmissionToken;
        m_laggedLightingHistoryWriterDrainGeneration = m_laggedLightingHistoryGeneration;
    }
    m_frameLaggedAsyncLightingEnabled = enabled;
    m_laggedLightingReport = LaggedLightingReport::Unreported;
    m_laggedLightingReportGeneration = 0u;
    resetLaggedLightingHistoryReadTracking();
}

Core::GpuTaskGraphRuntimeStatistics RendererFramePipeline::deferredTaskGraphRuntimeStatistics()const noexcept{
    const Core::GpuCompiledGraph::ReadView deferredCompiledPlan(m_deferredLightingCompiledGraph);

    return Core::CollectGpuTaskGraphRuntimeStatistics(m_deferredLightingCompiledGraph, deferredCompiledPlan, m_deferredLightingRecordedGraph, m_deferredLightingSubmissionTransaction);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererFramePipeline::resetTargetGenerationStateHandoffs()noexcept{
    // Replaced targets invalidate retained compute-local state.
    m_raytracingSystem.invalidateLightSpaceShadowCapture();
    m_shadowComputePersistentState.reset();
    m_shadowVisibilityReturnState.reset();
    m_causticsComputePersistentState.reset();
    m_hardwareCausticAccumulatorPersistentState.reset();
    m_causticIrradianceReturnState.reset();
    m_surfelGiComputePersistentState.reset();
    m_surfelGiCounterPersistentState.reset();
    m_surfelIrradianceReturnState.reset();
}

void RendererFramePipeline::resetInvalidatedResourceStateHandoffs()noexcept{
    // Only retained cross-frame state resets here.
    resetTargetGenerationStateHandoffs();
    m_shadowPreparePersistentState.reset();
}

void RendererFramePipeline::invalidateLaggedLightingHistoryWriterDrain()noexcept{
    m_laggedLightingHistoryWriterDrainToken = Core::QueueSubmissionToken{};
    m_laggedLightingHistoryWriterDrainGeneration = 0u;
}

void RendererFramePipeline::resetLaggedLightingHistoryReadTracking()noexcept{
    invalidateLaggedLightingHistorySubmission();
    m_laggedLightingHistoryGeneration = 0u;
}

void RendererFramePipeline::resetLaggedLightingHistoryTracking()noexcept{
    resetLaggedLightingHistoryReadTracking();
    invalidateLaggedLightingHistoryWriterDrain();
}

void RendererFramePipeline::reportLaggedLightingTransition(const LaggedLightingReport::Enum report, const u64 targetGeneration){
    DeferredFrameDiagnostics{}.reportLaggedLightingTransitionIfNeeded(report, targetGeneration, m_laggedLightingReport, m_laggedLightingReportGeneration);
}


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


void RendererFramePipeline::render(Core::Framebuffer* framebuffer){
    m_frameGraphSourceFrameIndex = m_graphics.getFrameIndex();

    // Preserve the accepted frontier; artifacts below reset for the next frame.
    if(m_deferredLightingTaskGraphScheduled){
        Core::Alloc::ScratchArena queueAssignmentTelemetryScratchArena(s_TaskGraphArena);
        const Core::GpuTaskGraph::DeclarationReadView declarations(m_deferredLightingTaskGraph);
        const Core::GpuCompiledGraph::ReadView compiledPlan(m_deferredLightingCompiledGraph);
        if(!m_deferredLightingTaskGraphQueueAssignmentTelemetry.update(declarations, m_deferredLightingTaskGraphQueueAssignments, compiledPlan, m_deferredLightingSubmissionTransaction, queueAssignmentTelemetryScratchArena))
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: deferred queue-assignment history refresh failed before graph reset"));
    }

    resetFrameTaskState();

    if(!framebuffer)
        return;

    const Core::AcquiredPresentationFrame presentationFrame = m_graphics.acquiredPresentationFrame();
    const Core::FramebufferDesc& presentationFramebufferDesc = framebuffer->getDescription();
    if(!presentationFrame.valid() || presentationFrame.framebuffer.get() != framebuffer || presentationFramebufferDesc.colorAttachments.size() != 1u || presentationFramebufferDesc.colorAttachments[0].texture != presentationFrame.backBuffer.texture.get()){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: render target did not match the acquired presentation frame; requesting recreation"));
        m_graphics.requestDeviceRecreation();
        return;
    }

    if(!m_frameTargets.valid())
        return;
    DeferredFrameTargets& deferredTargets = m_frameTargets;

    NWB_ASSERT(m_preparedCsgFrameStateValid);
    if(!m_shadowPrepareGraph.m_shadowPreparationOutcome.resourcesValid){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: shadow-visibility resource preflight was unavailable"));
        return;
    }

    const CsgFrameState csgFrameState = m_preparedCsgFrameState;
    const b8 hasOpaqueCsgFrameWork = csgFrameState.hasOpaqueStaticWork || csgFrameState.hasOpaqueSkinnedWork;
    const b8 hasTransparentRenderers = m_preparedHasTransparentRenderers;
    NWB_ASSERT(csgFrameState.empty() || deferredTargets.csgIntervalTargetsValid());
    auto& device = m_graphics.getDevice();
    if(m_graphics.isDeviceRecreationRequested() || device.requiresRecreation()){
        if(device.requiresRecreation())
            m_graphics.requestDeviceRecreation();
        return;
    }
    if(m_frameRenderRecoveryFailed){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: frame render recovery failed; rendering is suspended until resources are recreated"));
        return;
    }
    Core::GpuDescriptorHeap::PendingRecordingLease descriptorHeapPendingRecordingLease = device.getDescriptorHeap().acquirePendingRecordingLease();
    if(!descriptorHeapPendingRecordingLease.valid()){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: descriptor heap pending-recording lease was unavailable; requesting recreation"));
        m_graphics.requestDeviceRecreation();
        return;
    }
    // Use the device's primary physical Graphics queue for scheduling.
    const Core::GpuPhysicalQueueId primaryGraphicsQueue = device.getPrimaryPhysicalQueue(Core::CommandQueue::Graphics);
    const u32 graphicsFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Graphics);
    const u32 computeFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Compute);
    const b8 dedicatedAsyncCompute = computeFamilyIndex != Limit<u32>::s_Max && computeFamilyIndex != graphicsFamilyIndex;
    const b8 laggedAsyncLightingRequested = m_frameLaggedAsyncLightingEnabled && dedicatedAsyncCompute;
    const b8 laggedLightingHistoryResourcesReady = deferredTargets.laggedLightingHistory.valid();
    if(laggedAsyncLightingRequested && !laggedLightingHistoryResourcesReady){
        NWB_ASSERT(laggedLightingHistoryResourcesReady);
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: lagged async lighting requires validated history targets"));
        return;
    }

    const u64 laggedLightingHistoryTargetGeneration = deferredTargets.laggedLightingHistory.generation;
    const Core::QueueSubmissionToken laggedLightingHistorySubmissionToken = m_laggedLightingHistorySubmissionToken;
    const auto laggedLightingHistoryTokenPending = [&device, laggedLightingHistoryTargetGeneration](const Core::QueueSubmissionToken& token, const u64 tokenTargetGeneration){
        if(!token.valid() || tokenTargetGeneration != laggedLightingHistoryTargetGeneration || !device.matchesPhysicalQueueIdentity(token.queue, token.physicalQueueIndex, token.deviceGeneration))
            return false;
        // A failed query keeps the writer pending.
        const Expected<u64> completedValue = device.queueGetCompletedInstance(Core::GpuPhysicalQueueId{ .index = token.physicalQueueIndex, .deviceGeneration = token.deviceGeneration, });
        return !completedValue || *completedValue < token.value;
    };
    const b8 laggedLightingHistorySubmissionPending = laggedLightingHistoryTokenPending(laggedLightingHistorySubmissionToken, m_laggedLightingHistoryGeneration);

    // Keep only an incomplete accepted tail as the writer drain.
    if(laggedLightingHistorySubmissionPending){
        m_laggedLightingHistoryWriterDrainToken = laggedLightingHistorySubmissionToken;
        m_laggedLightingHistoryWriterDrainGeneration = m_laggedLightingHistoryGeneration;
    }
    else if(m_laggedLightingHistoryWriterDrainToken.valid() && !laggedLightingHistoryTokenPending(m_laggedLightingHistoryWriterDrainToken, m_laggedLightingHistoryWriterDrainGeneration))
        invalidateLaggedLightingHistoryWriterDrain();

    if(laggedAsyncLightingRequested){
        if(m_laggedLightingHistoryGeneration != laggedLightingHistoryTargetGeneration){
            // Generations block recycled slots after resize.
            invalidateLaggedLightingHistorySubmission();
            m_laggedLightingHistoryGeneration = laggedLightingHistoryTargetGeneration;
        }
    }
    else
        resetLaggedLightingHistoryReadTracking();
    // Snapshot the prior tail before declaration resets the token.
    const Core::QueueSubmissionToken priorLaggedLightingHistoryReadReadyToken = m_laggedLightingHistorySubmissionToken;
    const Core::QueueSubmissionToken priorLaggedLightingHistoryWriterDrainToken = m_laggedLightingHistoryWriterDrainToken;
    const b8 laggedLightingHistoryWriterWaitPending = priorLaggedLightingHistoryWriterDrainToken.valid();
    const b8 hardwareShadowSupported = m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct) && m_graphics.queryFeatureSupport(Core::Feature::RayQuery);
    const b8 laggedAsyncLightingSchedule = laggedAsyncLightingRequested && laggedLightingHistoryResourcesReady && priorLaggedLightingHistoryReadReadyToken.valid();
    // The history tail is optional; its failure must not break the frame.
    const b8 requestsLaggedLightingHistoryCapture = laggedAsyncLightingRequested;
    m_shadowPrepareGraph.m_shadowPreparationOutcome.ready = false;
    // Compile graphs first; the prefix records its five tasks natively.
    const RendererFrameGraphFeatures frameGraphFeatures{
        .frameLaggedAsyncLightingEnabled = m_frameLaggedAsyncLightingEnabled,
        .laggedLightingHistoryReady = laggedLightingHistoryResourcesReady,
        .laggedLightingHistoryReadReady = priorLaggedLightingHistoryReadReadyToken.valid(),
        .laggedLightingHistoryWriterWaitPending = laggedLightingHistoryWriterWaitPending,
        .hasTransparentRenderers = hasTransparentRenderers,
        .hardwareCaustics = hardwareShadowSupported,
    };
    m_raytracingSystem.discardSoftShadowTemporalHistory();

    // Preserve mirrors so rejected recordings retry exactly.
    const RayTracingFrameCpuStateSnapshot rayTracingCpuState = m_rayTracingState.captureFrameCpuState();
    const b8 avboitTargetsNeedClear = m_avboitSystem.captureTargetClearState();
    const b8 deferredBindlessSlotsUploaded = deferredTargets.bindless.slotsUploaded;
    const RayTracingShadowPreparationResourceSnapshot rayTracingShadowResources = m_raytracingSystem.snapshotShadowPreparationResources();
    const RayTracingDeferredGraphResourceSnapshot rayTracingGraphResources = m_raytracingSystem.snapshotDeferredGraphResources();
    const RayTracingSurfelPersistentResourceSnapshot rayTracingSurfelResources = m_raytracingSystem.snapshotSurfelPersistentResources();
    const FrameExecuteCpuRestore::Snapshot executeCpuRestoreSnapshot{ .rayTracing = rayTracingCpuState, .avboitTargetsNeedClear = avboitTargetsNeedClear, .deferredBindlessSlotsUploaded = deferredBindlessSlotsUploaded, };
    const auto restorePostGbufferPacketCpuState = [&](const b8 restoreBindlessSlots)noexcept{
        FrameExecuteCpuRestore::RestorePostGbufferPacketCpuState(executeCpuRestoreSnapshot, restoreBindlessSlots, m_meshSystem, m_deferredSystem, m_rayTracingState, m_avboitSystem, deferredTargets);
    };
    // Retain this token even if Surfel GI consumes the diagnostic.
    const Core::QueueSubmissionToken surfelCounterReadbackCompletionToken = rayTracingCpuState.surfelCountReadbackSubmissionToken;

    FrameExecuteTimingState timing(m_graphics);
    b8 shadowVisibilityOpaqueProduced = false;
    b8 shadowVisibilityTransparentTraceProduced = false;
    u32 shadowVisibilityOpaqueFrameIndex = 0u;
    const b8 clearAvboitTargets = m_avboitSystem.shouldClearTargets(hasTransparentRenderers);
    const Core::FramebufferInfoEx& meshViewFramebufferInfo = deferredTargets.framebuffer->getFramebufferInfo();
    f32 meshViewAspectRatio = Scene::s_CameraFallbackAspectRatio;
    if(meshViewFramebufferInfo.width != 0u && meshViewFramebufferInfo.height != 0u)
        meshViewAspectRatio = static_cast<f32>(meshViewFramebufferInfo.width) / static_cast<f32>(meshViewFramebufferInfo.height);
    Core::GpuGraphSubmissionTransaction::PayloadLifetimeScope graphPayloadLifetime(m_deferredLightingSubmissionTransaction, m_deferredLightingTaskGraph, m_deferredLightingCompiledGraph);
    // Graph build, its retry, and scheduling share one scratch arena.
    Core::Alloc::ScratchArena deferredGraphScratchArena(s_TaskGraphArena);
    const auto buildDeferredGraph = [&](const b8 captureLightingHistory){
        buildDeferredLightingTaskGraph(
            deferredGraphScratchArena,
            frameGraphFeatures,
            csgFrameState,
            clearAvboitTargets,
            hasTransparentRenderers,
            hasOpaqueCsgFrameWork,
            meshViewAspectRatio,
            presentationFrame,
            timing.asyncPrefixTimingSpansOnePacket,
            surfelCounterReadbackCompletionToken,
            priorLaggedLightingHistoryReadReadyToken,
            priorLaggedLightingHistoryWriterDrainToken,
            captureLightingHistory,
            deferredTargets,
            timing.frameTimingTransaction,
            timing.asyncPrefixTiming,
            timing.deferredClearTiming,
            timing.deferredClearTimingState,
            timing.opaqueCsgIntervalClearTimingState,
            timing.opaqueRegularSharedComputeEmulationTiming,
            timing.opaqueCsgIntervalSampleComputeEmulationTiming,
            timing.shadowPrepareTimingTicket,
            timing.graphicsPrefixTimingTickets,
            timing.asyncFinalTiming,
            timing.avboitPreTimingTicket,
            timing.avboitClearTimingState,
            timing.transparentCsgIntervalClearTimingState,
            timing.transparentCsgIntervalsTiming,
            timing.avboitOccupancyComputeEmulationTiming,
            timing.avboitExtinctionComputeEmulationTiming,
            timing.avboitAccumulationComputeEmulationTiming,
            timing.avboitDepthWarpTimingTicket,
            timing.avboitExtinctionTimingTicket,
            timing.avboitIntegrationTimingTicket,
            timing.avboitAccumulationTimingTicket,
            timing.shadowVisibilityTimingTicket,
            timing.shadowVisibilityAsyncTiming,
            timing.shadowVisibilityTiming,
            timing.opaqueSoftResolveTiming,
            timing.transparentSoftResolveTiming,
            shadowVisibilityOpaqueProduced,
            shadowVisibilityTransparentTraceProduced,
            shadowVisibilityOpaqueFrameIndex,
            timing.softwareCausticsTimingTicket,
            timing.surfelGiTimingTicket,
            timing.surfelGiAsyncTiming,
            timing.hardwareCausticsTimingTicket,
            timing.causticPhotonTiming,
            timing.causticResolveTiming,
            timing.deferredLightingTimingTicket,
            timing.deferredCompositeTimingTicket,
            timing.deferredPresentTimingTicket
        );
    };
    buildDeferredGraph(requestsLaggedLightingHistoryCapture);
    if(requestsLaggedLightingHistoryCapture && !m_deferredLightingTaskGraphDeclared){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: deferred graph build with optional lagged lighting-history capture failed; retrying without the tail"));
        buildDeferredGraph(false);
    }
    const b8 deferredGraphScheduled = scheduleDeferredLightingTaskGraphForExecution(deferredGraphScratchArena);
    const Core::GpuTaskGraph::DeclarationReadView deferredTaskGraphView(m_deferredLightingTaskGraph);
    const Core::GpuCompiledGraph::ReadView deferredCompiledPlan(m_deferredLightingCompiledGraph);
    const b8 captureLaggedLightingHistory = m_deferredLaggedLightingHistoryTask.valid();
    const auto taskIsCompiled = [&](const Core::GpuTaskId task)noexcept{
        return deferredCompiledPlan.findTask(task).valid();
    };
    DeferredFrameTopologyResult topology;
    DeferredFrameTopologyValidator topologyValidator(*this);
    if(!topologyValidator.validate(
        deferredCompiledPlan,
        deferredTaskGraphView,
        timing,
        primaryGraphicsQueue,
        deferredGraphScheduled,
        captureLaggedLightingHistory,
        hardwareShadowSupported,
        hasOpaqueCsgFrameWork,
        hasTransparentRenderers,
        clearAvboitTargets,
        laggedAsyncLightingSchedule,
        laggedLightingHistoryWriterWaitPending,
        surfelCounterReadbackCompletionToken.valid(),
        topology
    )){
        timing.discardTickets();
        // No packet accepted yet; restore the CPU-only classification.
        m_rayTracingState.restorePreparedLightingCpuState(rayTracingCpuState);
        return;
    }
    const auto& graphicsPrefixTimingTasks = topology.graphicsPrefixTimingTasks;
    const usize graphicsPrefixUniquePacketCount = topology.graphicsPrefixUniquePacketCount;
    const RendererAvboitTaskGraphValidation& avboitValidation = topology.avboitValidation;
    const Core::GpuTaskId terminalPresentationTask = topology.terminalPresentationTask;
    const Core::GpuPhysicalQueueInfo* const surfelGiCounterReadbackQueue = topology.surfelGiCounterReadbackQueue;
    const Core::GpuPhysicalQueueInfo* const deferredLaggedLightingHistoryQueue = topology.laggedLightingHistoryQueue;
    FrameExecuteRecovery recovery(*this, deferredGraphScratchArena, timing.frameTimingTransaction, deferredCompiledPlan);
    const auto discardRenderPackets = [&](){
        timing.discardAll();
        if(!recovery.discardUnacceptedGraphPackets()){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: deferred graph cancellation overlapped active native work; requesting device recreation"));
            m_graphics.requestDeviceRecreation();
        }
        const b8 shadowPrepareAccepted = taskIsCompiled(m_shadowPrepareGraph.m_deferredShadowPrepareTask) && m_deferredLightingSubmissionTransaction.taskToken(deferredCompiledPlan, m_shadowPrepareGraph.m_deferredShadowPrepareTask).valid();
        restorePostGbufferPacketCpuState(!shadowPrepareAccepted);
        m_raytracingSystem.discardSoftShadowTemporalHistory();
    };

    FrameExecuteStateLifecycleSet lifecycles(*this, deferredTargets, rayTracingShadowResources, rayTracingGraphResources, rayTracingSurfelResources, hardwareShadowSupported, laggedAsyncLightingSchedule);
    m_avboitSystem.markFrameTargetUsage(hasTransparentRenderers);

    Array<Core::GpuTaskGraphTaskTimingTicket, s_DeferredTimingTicketCapacity> normalTimingTickets = {};
    const auto normalTimingTicketCount = timing.bindNormalTimingTickets(*this, deferredCompiledPlan, graphicsPrefixTimingTasks, graphicsPrefixUniquePacketCount, avboitValidation, hardwareShadowSupported, normalTimingTickets);
    if(!normalTimingTicketCount){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to bind normal deferred graph timing"));
        discardRenderPackets();
        return;
    }

    // Empty hooks use direct presentation for standalone passes.
    const Core::QueueSubmissionPreSubmitHook framePresentationSignal = m_graphics.claimFramePresentationSignal();
    const Array<Core::GpuTaskGraphTaskSubmissionHook, 1> terminalPresentationSubmissionHooks = { Core::GpuTaskGraphTaskSubmissionHook{ .task = terminalPresentationTask, .hook = framePresentationSignal, }, };
    Core::GpuTaskGraphNormalExecutionDesc normalExecution;
    normalExecution.terminalTask = terminalPresentationTask;
    normalExecution.taskRecordedCallbacks = lifecycles.recordedCallbacks.data();
    normalExecution.taskRecordedCallbackCount = lifecycles.recordedCallbackCount;
    normalExecution.readyFrontierScheduler = &m_world.taskScheduler();
    normalExecution.taskTimingTickets = normalTimingTickets.data();
    normalExecution.taskTimingTicketCount = *normalTimingTicketCount;
    normalExecution.taskAcceptedCallbacks = lifecycles.acceptedCallbacks.data();
    normalExecution.taskAcceptedCallbackCount = lifecycles.acceptedCallbackCount;
    normalExecution.taskSubmissionHooks = framePresentationSignal.valid() ? terminalPresentationSubmissionHooks.data() : nullptr;
    normalExecution.taskSubmissionHookCount = framePresentationSignal.valid() ? terminalPresentationSubmissionHooks.size() : 0u;

    const Core::GpuTaskScheduler& normalScheduler = m_graphics.gpuTasks();
    const b8 normalGraphAccepted = normalScheduler.executeGraph(
        normalExecution,
        lifecycles.normalExecutionScratchArena,
        m_deferredLightingTaskGraph,
        m_deferredLightingCompiledGraph,
        m_deferredLightingRecordedGraph,
        m_deferredLightingSubmissionTransaction,
        &m_graphics.gpuTiming()
    );

    const Core::QueueSubmissionToken frameTimingBeginSubmissionToken = m_deferredLightingSubmissionTransaction.taskToken(deferredCompiledPlan, m_deferredFrameTimingBeginTask);
    const Core::QueueSubmissionToken shadowPrepareSubmissionToken = m_deferredLightingSubmissionTransaction.taskToken(deferredCompiledPlan, m_shadowPrepareGraph.m_deferredShadowPrepareTask);
    const Core::QueueSubmissionToken graphicsPrefixSubmissionToken = m_deferredLightingSubmissionTransaction.taskToken(deferredCompiledPlan, m_graphicsPrefixGraph.m_graphicsPrefixTask);
    const Core::QueueSubmissionToken shadowVisibilitySubmissionToken = m_deferredLightingSubmissionTransaction.taskToken(deferredCompiledPlan, m_shadowVisibilityGraph.m_deferredShadowVisibilityTask);
    const Core::GpuTaskId selectedCausticsTask = hardwareShadowSupported ? m_causticsGraph.m_deferredHardwareCausticsTask : m_causticsGraph.m_deferredSoftwareCausticsTask;
    const Core::QueueSubmissionToken selectedCausticsSubmissionToken = m_deferredLightingSubmissionTransaction.taskToken(deferredCompiledPlan, selectedCausticsTask);
    const Core::QueueSubmissionToken surfelGiSubmissionToken = m_deferredLightingSubmissionTransaction.taskToken(deferredCompiledPlan, m_surfelGiGraph.m_deferredSurfelGiTask);
    const Core::QueueSubmissionToken avboitPreSubmissionToken = m_deferredLightingSubmissionTransaction.taskToken(deferredCompiledPlan, avboitValidation.stage().firstTask);
    const Core::QueueSubmissionToken avboitCompletionSubmissionToken = m_deferredLightingSubmissionTransaction.taskToken(deferredCompiledPlan, avboitValidation.stage().completionTask);
    const Core::QueueSubmissionToken deferredLightingSubmissionToken = m_deferredLightingSubmissionTransaction.taskToken(deferredCompiledPlan, m_deferredLightingTask);
    const Core::QueueSubmissionToken deferredCompositeSubmissionToken = m_deferredLightingSubmissionTransaction.taskToken(deferredCompiledPlan, m_deferredCompositeTask);
    const Core::QueueSubmissionToken deferredPresentSubmissionToken = m_deferredLightingSubmissionTransaction.taskToken(deferredCompiledPlan, m_deferredPresentTask);
    const Core::QueueSubmissionToken finalPresentationSubmissionToken = m_deferredLightingSubmissionTransaction.taskToken(deferredCompiledPlan, terminalPresentationTask);

    b8 presentationSignalReady = true;
    if(framePresentationSignal.valid()){
        if(finalPresentationSubmissionToken.valid()){
            presentationSignalReady = m_graphics.confirmFramePresentationSignal(framePresentationSignal, finalPresentationSubmissionToken);
            if(!presentationSignalReady && !m_graphics.cancelFramePresentationSignal(framePresentationSignal))
                m_graphics.requestDeviceRecreation();
        }
        else{
            presentationSignalReady = false;
            if(!m_graphics.cancelFramePresentationSignal(framePresentationSignal))
                m_graphics.requestDeviceRecreation();
        }
    }

    const b8 selectedCausticsStateReady = lifecycles.selectedCausticsStateReady(hardwareShadowSupported);
    const b8 normalSemanticTokensReady =
        frameTimingBeginSubmissionToken.valid()
        && frameTimingBeginSubmissionToken.matchesPhysicalQueue(primaryGraphicsQueue.index, primaryGraphicsQueue.deviceGeneration)
        && shadowPrepareSubmissionToken.valid()
        && graphicsPrefixSubmissionToken.valid()
        && shadowVisibilitySubmissionToken.valid()
        && selectedCausticsSubmissionToken.valid()
        && surfelGiSubmissionToken.valid()
        && avboitPreSubmissionToken.valid()
        && avboitCompletionSubmissionToken.valid()
        && deferredLightingSubmissionToken.valid()
        && deferredCompositeSubmissionToken.valid()
        && deferredPresentSubmissionToken.valid()
        && finalPresentationSubmissionToken.valid()
    ;
    const b8 normalStateReady =
        lifecycles.shadowPrepareStateLifecycle.stateReady
        && lifecycles.shadowVisibilityStateLifecycle.stateReady
        && selectedCausticsStateReady
        && lifecycles.surfelGiStateLifecycle.stateReady
        && lifecycles.deferredLightingStateLifecycle.stateReady;
    const b8 acceptedStateLost =
        (shadowPrepareSubmissionToken.valid() && !lifecycles.shadowPrepareStateLifecycle.stateReady)
        || (shadowVisibilitySubmissionToken.valid() && !lifecycles.shadowVisibilityStateLifecycle.stateReady)
        || (selectedCausticsSubmissionToken.valid() && !selectedCausticsStateReady)
        || (surfelGiSubmissionToken.valid() && !lifecycles.surfelGiStateLifecycle.stateReady)
        || (deferredLightingSubmissionToken.valid() && !lifecycles.deferredLightingStateLifecycle.stateReady)
    ;

    b8 frameTimingEndReady = true;
    if(finalPresentationSubmissionToken.valid()){
        frameTimingEndReady = timing.frameTimingTransaction.confirmEndSubmission(finalPresentationSubmissionToken, true);
        if(!frameTimingEndReady){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: failed to confirm frame critical-path timing"));
            timing.frameTimingTransaction.discard();
        }
    }
    if(!normalGraphAccepted || !normalSemanticTokensReady || !normalStateReady || !presentationSignalReady){
        const b8 hadAcceptedPackets = m_deferredLightingSubmissionTransaction.hasAcceptedPackets();
        b8 recovered = true;
        if(!hadAcceptedPackets)
            discardRenderPackets();
        else{
            recovered = recovery.recoverPendingFrameThenDiscardUnaccepted();
            timing.discardTickets();

            if(!shadowPrepareSubmissionToken.valid())
                restorePostGbufferPacketCpuState(true);
            else{
                if(!graphicsPrefixSubmissionToken.valid())
                    FrameExecuteCpuRestore::RestorePrefixCpuState(m_meshSystem, m_deferredSystem);
                if(!shadowVisibilitySubmissionToken.valid()){
                    m_rayTracingState.restoreShadowPacketCpuState(rayTracingCpuState);
                    m_raytracingSystem.discardSoftShadowTemporalHistory();
                }
                if(!selectedCausticsSubmissionToken.valid())
                    m_rayTracingState.restoreCausticPacketCpuState(rayTracingCpuState);
                if(!surfelGiSubmissionToken.valid())
                    m_rayTracingState.restoreSurfelGiPacketCpuState(rayTracingCpuState);
                if(!avboitPreSubmissionToken.valid())
                    m_avboitSystem.restoreTargetClearState(avboitTargetsNeedClear);
            }
        }

        if(deferredPresentSubmissionToken.valid() && !finalPresentationSubmissionToken.valid()){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: acquired back buffer was written before presentation suffix rejection; requesting recreation"));
            m_graphics.requestDeviceRecreation();
        }
        if(!presentationSignalReady && finalPresentationSubmissionToken.valid())
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: terminal graph presentation signal confirmation failed"));
        else
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: graph-owned normal deferred execution was rejected"));
        if(!recovered || acceptedStateLost || (!presentationSignalReady && finalPresentationSubmissionToken.valid()))
            recovery.failFrameRenderRecovery();
        return;
    }

    if(!dedicatedAsyncCompute && m_frameLaggedAsyncLightingEnabled)
        reportLaggedLightingTransition(LaggedLightingReport::NoDedicatedAsyncCompute, deferredTargets.laggedLightingHistory.generation);
    else if(m_laggedLightingCurrentFrameAcceptancePending){
        reportLaggedLightingTransition(LaggedLightingReport::CurrentFrameAccepted, deferredTargets.laggedLightingHistory.generation);
        m_laggedLightingCurrentFrameAcceptancePending = false;
    }

    FrameExecutePresentTails presentTails(*this, deferredGraphScratchArena, recovery, deferredCompiledPlan, finalPresentationSubmissionToken);
    if(!presentTails.executeSurfelCounterReadback(surfelGiCounterReadbackQueue, lifecycles.surfelGiCounterBuffers))
        return;
    presentTails.executeLaggedLightingHistoryCapture(deferredTargets, deferredLaggedLightingHistoryQueue, requestsLaggedLightingHistoryCapture, captureLaggedLightingHistory, laggedAsyncLightingSchedule);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererFramePipeline::appendFrameGraph(Core::Telemetry::FrameGraphBuilder& builder){
    if(!m_frameTargets.valid())
        return false;

    const Core::GpuTaskGraph::DeclarationReadView deferredTaskGraphView(m_deferredLightingTaskGraph);
    const Core::GpuCompiledGraph::ReadView deferredCompiledPlan(m_deferredLightingCompiledGraph);

    using Handle = Core::Telemetry::FrameGraphNodeHandle;
    namespace Edge = Core::Telemetry::FrameGraphEdgeKind;

    const CsgFrameState& csgFrameState = m_preparedCsgFrameState;
    const b8 hasCsgFrameWork = m_preparedCsgFrameStateValid && !csgFrameState.empty();
    const b8 hasOpaqueCsgFrameWork = hasCsgFrameWork && (csgFrameState.hasOpaqueStaticWork || csgFrameState.hasOpaqueSkinnedWork);
    const b8 hasTransparentCsgFrameWork = hasCsgFrameWork && (csgFrameState.hasTransparentStaticWork || csgFrameState.hasTransparentSkinnedWork);
    const b8 hasTransparentRenderers = m_materialSystem.hasTransparentRenderers(RendererResourceLookupMode::PreparedOnly);
    const b8 hasAvboitWork = m_avboitSystem.shouldClearTargets(hasTransparentRenderers);

    Core::GpuTaskGraphRuntimeStatistics deferredRuntimeStatistics{};
    if(builder.frameIndex() == m_frameGraphSourceFrameIndex)
        deferredRuntimeStatistics = deferredTaskGraphRuntimeStatistics();
    Core::Telemetry::FrameGraphPassMetadata rendererFrameMetadata;
    rendererFrameMetadata.runtimeStatistics = ECSRenderDetail::BuildFrameGraphRuntimeStatistics(deferredRuntimeStatistics, builder.frameIndex(), m_frameGraphSourceFrameIndex);
    const Core::GpuPhysicalQueueTopology runtimeQueueTopology = deferredCompiledPlan.queueTopology();
    Core::Alloc::ScratchArena& scratchArena = builder.scratchArena();
    Vector< Core::Telemetry::FrameGraphPhysicalQueueRuntimeStatistics, Core::Alloc::ScratchArena > physicalQueueRuntimeStatistics(scratchArena);
    Vector< Core::GpuTaskGraphPacketSubmissionStatistics, Core::Alloc::ScratchArena > packetSubmissionStatistics(scratchArena);
    if(rendererFrameMetadata.runtimeStatistics.present && (!runtimeQueueTopology.queues || runtimeQueueTopology.queueCount == 0u))
        rendererFrameMetadata.runtimeStatistics = {};
    if(rendererFrameMetadata.runtimeStatistics.present){
        physicalQueueRuntimeStatistics.reserve(runtimeQueueTopology.queueCount);
        for(usize queueIndex = 0u; queueIndex < runtimeQueueTopology.queueCount; ++queueIndex){
            const Core::GpuPhysicalQueueId queue = runtimeQueueTopology.queues[queueIndex].id;
            const Core::Telemetry::FrameGraphPhysicalQueueRuntimeStatistics queueStatistics =
                ECSRenderDetail::BuildFrameGraphPhysicalQueueRuntimeStatistics(
                    deferredCompiledPlan.physicalQueueCompileStatistics(queue),
                    m_deferredLightingRecordedGraph.physicalQueueRecordingStatistics(m_deferredLightingCompiledGraph, deferredCompiledPlan, queue),
                    m_deferredLightingSubmissionTransaction.physicalQueueSubmissionStatistics(deferredCompiledPlan, queue)
                )
            ;
            if(!Core::Telemetry::IsValidFrameGraphPhysicalQueueRuntimeStatisticsForOwner(queueStatistics, rendererFrameMetadata.runtimeStatistics)){
                rendererFrameMetadata.runtimeStatistics = {};
                physicalQueueRuntimeStatistics.clear();
                break;
            }
            physicalQueueRuntimeStatistics.push_back(queueStatistics);
        }
    }
    if(rendererFrameMetadata.runtimeStatistics.present){
        packetSubmissionStatistics.reserve(deferredCompiledPlan.packetCount());
        for(usize packetIndex = 0u; packetIndex < deferredCompiledPlan.packetCount(); ++packetIndex){
            const Core::GpuSubmissionPacketId packet = deferredCompiledPlan.packetIdAt(packetIndex);
            if(!packet.valid()){
                rendererFrameMetadata.runtimeStatistics = {};
                physicalQueueRuntimeStatistics.clear();
                packetSubmissionStatistics.clear();
                break;
            }

            const Core::GpuTaskGraphPacketSubmissionStatistics packetStatistics = m_deferredLightingSubmissionTransaction.packetSubmissionStatistics(deferredCompiledPlan, packet);
            if(packetStatistics.valid())
                packetSubmissionStatistics.push_back(packetStatistics);
        }
        if(rendererFrameMetadata.runtimeStatistics.present && packetSubmissionStatistics.size() != deferredRuntimeStatistics.submission.nativeSubmissionCount){
            rendererFrameMetadata.runtimeStatistics = {};
            physicalQueueRuntimeStatistics.clear();
            packetSubmissionStatistics.clear();
        }
    }
    m_frameGraphRendererLabel.clear();
    if(!RendererGraphRuntimeLabel::Append(deferredRuntimeStatistics, runtimeQueueTopology, physicalQueueRuntimeStatistics, deferredCompiledPlan, m_deferredLightingRecordedGraph, m_graphics.getDevice(), m_frameGraphRendererLabel))
        return false;

    RendererDeviceDiagnosticsLabel::Append(m_graphics, m_frameGraphRendererLabel);

    const Handle rendererFrame = builder.addPass(Name(s_FrameGraphNameEcsRenderFrame), AStringView(m_frameGraphRendererLabel.data(), m_frameGraphRendererLabel.size()), rendererFrameMetadata);
    for(const Core::Telemetry::FrameGraphPhysicalQueueRuntimeStatistics& queueStatistics : physicalQueueRuntimeStatistics){
        if(!builder.addPhysicalQueueRuntimeStatistics(rendererFrame, queueStatistics)){
            NWB_ASSERT(false);
            return false;
        }
    }
    for(const Core::GpuTaskGraphPacketSubmissionStatistics& packetStatistics : packetSubmissionStatistics){
        const Core::Telemetry::FrameGraphPacketSubmissionStatisticsRecord telemetryStatistics = ECSRenderDetail::BuildFrameGraphPacketSubmissionStatistics(packetStatistics, rendererFrame.index);
        if(!builder.addPacketSubmissionStatistics(rendererFrame, telemetryStatistics)){
            NWB_ASSERT(false);
            return false;
        }
    }
    const Handle frameSetup = builder.addPass(Name(s_FrameGraphNameEcsRenderFrameSetup), "Frame Setup");
    const Handle deferredTargets = builder.addResource(Name(s_FrameGraphNameEcsRenderDeferredTargets), "Deferred Targets");
    const Handle meshViewBuffer = builder.addResource(Name(s_FrameGraphNameEcsRenderMeshViewBuffer), "Mesh View Buffer");
    const Handle sceneShadingBuffer = builder.addResource(Name(s_FrameGraphNameEcsRenderSceneShadingBuffer), "Scene Shading Buffer");
    const Handle materialBuffers = builder.addResource(Name(s_FrameGraphNameEcsRenderMaterialDrawBuffers), "Material Draw Buffers");
    const Handle backBuffer = builder.addExternal(Core::GraphicsFrameGraphNodes::s_Backbuffer, "Back Buffer");

    Handle lastPass = frameSetup;
    auto appendPass = [&](const Core::GpuTimingScopeDefinition& scope, const AStringView label) -> Handle{
        const Handle pass = builder.addPass(scope.identity, label);
        builder.addEdge(lastPass, pass, Edge::DependsOn);
        lastPass = pass;
        return pass;
    };

    builder.addEdge(rendererFrame, frameSetup, Edge::DependsOn);
    builder.addEdge(frameSetup, meshViewBuffer, Edge::Writes);
    builder.addEdge(frameSetup, sceneShadingBuffer, Edge::Writes);

    const Handle deferredClear = appendPass(RendererGpuTimingScope::s_DeferredClear, "Deferred Clear");
    builder.addEdge(deferredClear, deferredTargets, Edge::Writes);

    const Handle materialUpload = appendPass(RendererGpuTimingScope::s_MaterialUpload, "Material Upload");
    builder.addEdge(materialUpload, materialBuffers, Edge::Writes);

    Handle csgIntervalTargets;
    if(hasOpaqueCsgFrameWork){
        csgIntervalTargets = builder.addResource(Name(s_FrameGraphNameEcsRenderCsgIntervalTargets), "CSG Interval Targets");

        const Handle csgUpload = appendPass(RendererGpuTimingScope::s_CsgUpload, "CSG Upload");
        builder.addEdge(csgUpload, csgIntervalTargets, Edge::Writes);

        const Handle csgSampleStateUpload = appendPass(RendererGpuTimingScope::s_CsgSampleStateUpload, "CSG Sample State Upload");
        builder.addEdge(csgSampleStateUpload, csgIntervalTargets, Edge::Writes);

        const Handle csgIntervalPeel = appendPass(RendererGpuTimingScope::s_CsgIntervalPeel, "CSG Interval Peel");
        builder.addEdge(csgIntervalPeel, csgIntervalTargets, Edge::Writes);

        const Handle csgReceiverSurface = appendPass(RendererGpuTimingScope::s_OpaqueCsgReceiverSurface, "Opaque CSG Receiver Surface");
        builder.addEdge(meshViewBuffer, csgReceiverSurface, Edge::Reads);
        builder.addEdge(csgReceiverSurface, csgIntervalTargets, Edge::Writes);

        const Handle csgReceiverSpanBuild = appendPass(RendererGpuTimingScope::s_CsgReceiverSpanBuild, "CSG Receiver Span Build");
        builder.addEdge(csgIntervalTargets, csgReceiverSpanBuild, Edge::Reads);
        builder.addEdge(csgReceiverSpanBuild, csgIntervalTargets, Edge::Writes);

        const Handle csgIntervalCombine = appendPass(RendererGpuTimingScope::s_CsgIntervalCombine, "CSG Interval Combine");
        builder.addEdge(csgIntervalTargets, csgIntervalCombine, Edge::Reads);
        builder.addEdge(csgIntervalCombine, csgIntervalTargets, Edge::Writes);
    }

    const Handle opaqueRegular = appendPass(RendererGpuTimingScope::s_OpaqueRegular, "Opaque Regular");
    builder.addEdge(meshViewBuffer, opaqueRegular, Edge::Reads);
    builder.addEdge(sceneShadingBuffer, opaqueRegular, Edge::Reads);
    builder.addEdge(materialBuffers, opaqueRegular, Edge::Reads);
    builder.addEdge(opaqueRegular, deferredTargets, Edge::Writes);

    if(hasOpaqueCsgFrameWork){
        const Handle opaqueCsg = appendPass(RendererGpuTimingScope::s_OpaqueCsg, "Opaque CSG");
        builder.addEdge(meshViewBuffer, opaqueCsg, Edge::Reads);
        builder.addEdge(materialBuffers, opaqueCsg, Edge::Reads);
        builder.addEdge(csgIntervalTargets, opaqueCsg, Edge::Reads);
        builder.addEdge(opaqueCsg, deferredTargets, Edge::Writes);

        const Handle csgCapFill = appendPass(RendererGpuTimingScope::s_CsgCapFill, "CSG Cap Fill");
        builder.addEdge(csgIntervalTargets, csgCapFill, Edge::Reads);
        builder.addEdge(csgCapFill, deferredTargets, Edge::Writes);
    }

    Handle avboitTargets;
    if(hasAvboitWork){
        avboitTargets = builder.addResource(Name(s_FrameGraphNameEcsRenderAvboitTargets), "AVBOIT Targets");

        const Handle avboitClear = appendPass(RendererGpuTimingScope::s_AvboitClear, "AVBOIT Clear");
        builder.addEdge(avboitClear, avboitTargets, Edge::Writes);
    }
    if(hasTransparentRenderers){
        if(hasTransparentCsgFrameWork){
            const Handle transparentCsgIntervals = appendPass(RendererGpuTimingScope::s_TransparentCsgIntervals, RendererGpuPassText::s_TransparentCsgIntervalsLabel);
            builder.addEdge(deferredTargets, transparentCsgIntervals, Edge::Reads);
            builder.addEdge(transparentCsgIntervals, avboitTargets, Edge::Writes);
        }

        const Handle avboitOccupancy = appendPass(RendererGpuTimingScope::s_AvboitOccupancy, "AVBOIT Occupancy");
        builder.addEdge(meshViewBuffer, avboitOccupancy, Edge::Reads);
        builder.addEdge(materialBuffers, avboitOccupancy, Edge::Reads);
        builder.addEdge(avboitOccupancy, avboitTargets, Edge::Writes);

        const Handle avboitDepthWarp = appendPass(RendererGpuTimingScope::s_AvboitDepthWarp, RendererGpuPassText::s_AvboitDepthWarpLabel);
        builder.addEdge(avboitTargets, avboitDepthWarp, Edge::Reads);
        builder.addEdge(avboitDepthWarp, avboitTargets, Edge::Writes);

        const Handle avboitExtinction = appendPass(RendererGpuTimingScope::s_AvboitExtinction, RendererGpuPassText::s_AvboitExtinctionLabel);
        builder.addEdge(avboitTargets, avboitExtinction, Edge::Reads);
        builder.addEdge(avboitExtinction, avboitTargets, Edge::Writes);

        const Handle avboitIntegration = appendPass(RendererGpuTimingScope::s_AvboitIntegration, RendererGpuPassText::s_AvboitIntegrationLabel);
        builder.addEdge(avboitTargets, avboitIntegration, Edge::Reads);
        builder.addEdge(avboitIntegration, avboitTargets, Edge::Writes);

        const Handle avboitAccumulate = appendPass(RendererGpuTimingScope::s_AvboitAccumulate, "AVBOIT Accumulate");
        builder.addEdge(avboitTargets, avboitAccumulate, Edge::Reads);
        builder.addEdge(avboitAccumulate, deferredTargets, Edge::Writes);
    }

    const Handle deferredLighting = appendPass(RendererGpuTimingScope::s_DeferredLighting, RendererGpuPassText::s_DeferredLightingLabel);
    builder.addEdge(deferredTargets, deferredLighting, Edge::Reads);
    builder.addEdge(deferredLighting, deferredTargets, Edge::Writes);

    const Handle deferredComposite = appendPass(RendererGpuTimingScope::s_DeferredComposite, RendererGpuPassText::s_DeferredCompositeLabel);
    builder.addEdge(deferredTargets, deferredComposite, Edge::Reads);

    const Handle deferredPresent = appendPass(RendererGpuTimingScope::s_DeferredPresent, RendererGpuPassText::s_DeferredPresentLabel);
    builder.addEdge(deferredComposite, deferredPresent, Edge::DependsOn);
    builder.addEdge(deferredPresent, backBuffer, Edge::Writes);

    if(m_deferredLightingTaskGraphScheduled){
        if(!m_deferredLightingTaskGraphQueueAssignmentTelemetry.update(deferredTaskGraphView, m_deferredLightingTaskGraphQueueAssignments, deferredCompiledPlan, m_deferredLightingSubmissionTransaction, scratchArena))
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: accepted queue-assignment telemetry refresh failed; skipping detailed task graph export"));
        else{
            const Core::GpuTaskGraphTelemetryOptions deferredLightingTelemetryOptions{
                .queueAssignments = &m_deferredLightingTaskGraphQueueAssignments,
                .compiledPlan = &deferredCompiledPlan,
                .queueAssignmentTelemetry = &m_deferredLightingTaskGraphQueueAssignmentTelemetry,
            };
            if(!deferredTaskGraphView.appendFrameGraphTelemetry(m_deferredLightingTaskGraphAnalysis, scratchArena, builder, deferredLightingTelemetryOptions))
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: deferred-effects/lighting/composite/present graph telemetry export failed"));
        }
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
