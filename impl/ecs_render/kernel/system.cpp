// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/kernel/system.h>

#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/kernel/renderer_private.h>

#include <impl/ecs_scene/components.h>

#include <core/graphics/gpu_timing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RendererSystem::RendererSystem(
    Core::Alloc::GlobalArena& arena,
    Core::ECS::World& world,
    Core::Graphics& graphics,
    Core::Assets::AssetManager& assetManager,
    ShaderPathResolveCallback shaderPathResolver
)
    : Core::ECS::ISystem(arena)
    , Core::IRenderPass(graphics)
    , m_arena(arena)
    , m_world(world)
    , m_graphics(graphics)
    , m_assetManager(assetManager)
    , m_shaderPathResolver(Move(shaderPathResolver))
    , m_csgShapeRegistry(arena)
    , m_graphicsPrefixTaskGraph(arena)
    , m_graphicsPrefixTaskGraphAnalysis(arena)
    , m_graphicsPrefixTaskGraphQueueAssignments(arena)
    , m_graphicsPrefixCompiledGraph(arena)
    , m_graphicsPrefixRecordedGraph(arena)
    , m_graphicsPrefixSubmissionTransaction(arena)
    , m_laggedLightingHistoryTaskGraph(arena)
    , m_laggedLightingHistoryTaskGraphAnalysis(arena)
    , m_laggedLightingHistoryTaskGraphQueueAssignments(arena)
    , m_laggedLightingHistoryCompiledGraph(arena)
    , m_laggedLightingHistoryRecordedGraph(arena)
    , m_laggedLightingHistorySubmissionTransaction(arena)
    , m_deferredCompositeTaskGraph(arena)
    , m_deferredCompositeTaskGraphAnalysis(arena)
    , m_deferredCompositeTaskGraphQueueAssignments(arena)
    , m_deferredCompositeCompiledGraph(arena)
    , m_deferredCompositeRecordedGraph(arena)
    , m_deferredCompositeSubmissionTransaction(arena)
    , m_deferredPresentTaskGraph(arena)
    , m_deferredPresentTaskGraphAnalysis(arena)
    , m_deferredPresentTaskGraphQueueAssignments(arena)
    , m_deferredPresentCompiledGraph(arena)
    , m_deferredPresentRecordedGraph(arena)
    , m_deferredPresentSubmissionTransaction(arena)
    , m_shadowVisibilityTaskGraph(arena)
    , m_shadowVisibilityTaskGraphAnalysis(arena)
    , m_shadowVisibilityTaskGraphQueueAssignments(arena)
    , m_shadowVisibilityCompiledGraph(arena)
    , m_shadowVisibilityRecordedGraph(arena)
    , m_shadowVisibilitySubmissionTransaction(arena)
    , m_hardwareCausticsTaskGraph(arena)
    , m_hardwareCausticsTaskGraphAnalysis(arena)
    , m_hardwareCausticsTaskGraphQueueAssignments(arena)
    , m_hardwareCausticsCompiledGraph(arena)
    , m_hardwareCausticsRecordedGraph(arena)
    , m_hardwareCausticsSubmissionTransaction(arena)
    , m_softwareCausticsTaskGraph(arena)
    , m_softwareCausticsTaskGraphAnalysis(arena)
    , m_softwareCausticsTaskGraphQueueAssignments(arena)
    , m_softwareCausticsCompiledGraph(arena)
    , m_softwareCausticsRecordedGraph(arena)
    , m_softwareCausticsSubmissionTransaction(arena)
    , m_surfelGiTaskGraph(arena)
    , m_surfelGiTaskGraphAnalysis(arena)
    , m_surfelGiTaskGraphQueueAssignments(arena)
    , m_surfelGiCompiledGraph(arena)
    , m_surfelGiRecordedGraph(arena)
    , m_surfelGiSubmissionTransaction(arena)
    , m_avboitTaskGraph(arena)
    , m_avboitTaskGraphAnalysis(arena)
    , m_avboitTaskGraphQueueAssignments(arena)
    , m_avboitCompiledGraph(arena)
    , m_avboitRecordedGraph(arena)
    , m_avboitSubmissionTransaction(arena)
    , m_deferredLightingTaskGraph(arena)
    , m_deferredLightingTaskGraphAnalysis(arena)
    , m_deferredLightingTaskGraphQueueAssignments(arena)
    , m_deferredLightingCompiledGraph(arena)
    , m_deferredLightingRecordedGraph(arena)
    , m_deferredLightingSubmissionTransaction(arena)
    , m_meshState(arena)
    , m_materialState(arena)
    , m_rayTracingState(arena)
    , m_shadowPrepareStateHandoff(arena)
    , m_meshViewSetupStateHandoff(arena)
    , m_sceneShadingSetupStateHandoff(arena)
    , m_deferredClearStateHandoff(arena)
    , m_frameSetupStateFanInHandoff(arena)
    , m_gbufferStateHandoff(arena)
    , m_postGbufferNormalizedStateHandoff(arena)
    , m_shadowComputeBaseStateHandoff(arena)
    , m_shadowComputeInputStateHandoff(arena)
    , m_shadowComputePersistentStateHandoff(arena)
    , m_shadowVisibilityStateHandoff(arena)
    , m_shadowVisibilityLightingStateHandoff(arena)
    , m_shadowVisibilityReturnStateHandoff(arena)
    , m_causticsComputeBaseStateHandoff(arena)
    , m_causticsComputeInputStateHandoff(arena)
    , m_causticsComputePersistentStateHandoff(arena)
    , m_causticsStateHandoff(arena)
    , m_causticIrradianceLightingStateHandoff(arena)
    , m_causticIrradianceReturnStateHandoff(arena)
    , m_surfelGiComputeBaseStateHandoff(arena)
    , m_surfelGiComputeInputStateHandoff(arena)
    , m_surfelGiComputePersistentStateHandoff(arena)
    , m_surfelGiStateHandoff(arena)
    , m_surfelIrradianceLightingStateHandoff(arena)
    , m_surfelIrradianceReturnStateHandoff(arena)
    , m_deferredLightingBaseStateHandoff(arena)
    , m_deferredLightingInputStateHandoff(arena)
    , m_deferredLightingStateHandoff(arena)
    , m_avboitLightingStateHandoff(arena)
    , m_avboitCompositeStateHandoff(arena)
    , m_opaqueColorCompositeStateHandoff(arena)
    , m_deferredCompositeBaseStateHandoff(arena)
    , m_deferredCompositeInputStateHandoff(arena)
    , m_deferredCompositeStateHandoff(arena)
    , m_compositeColorPresentStateHandoff(arena)
    , m_deferredPresentBaseStateHandoff(arena)
    , m_deferredPresentInputStateHandoff(arena)
    , m_deferredPresentStateHandoff(arena)
    , m_laggedLightingHistoryCopyInputStateHandoff(arena)
    , m_laggedLightingHistoryCopyStateHandoff(arena)
    , m_shaderSystem(*this)
    , m_meshSystem(*this)
    , m_materialSystem(*this)
    , m_csgSystem(*this)
    , m_deferredSystem(*this)
    , m_avboitSystem(*this)
    , m_raytracingSystem(*this)
{
    if(!RegisterBuiltInCsgShapeTypes(m_csgShapeRegistry))
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register built-in CSG shape types"));

    readAccess<NWB::Impl::Scene::ActiveCameraComponent>();
    readAccess<NWB::Impl::Scene::TransformComponent>();
    readAccess<NWB::Impl::Scene::CameraComponent>();
    readAccess<RendererComponent>();
    readAccess<MaterialInstanceComponent>();
    readAccess<StaticCsgMeshComponent>();
    readAccess<SkinnedCsgMeshComponent>();
    readAccess<CsgCutterComponent>();
}
RendererSystem::~RendererSystem(){}


void RendererSystem::reportLaggedLightingTransition(const LaggedLightingReport report, const u64 targetGeneration){
    if(m_laggedLightingReport == report && m_laggedLightingReportGeneration == targetGeneration)
        return;

    m_laggedLightingReport = report;
    m_laggedLightingReportGeneration = targetGeneration;
    switch(report){
    case LaggedLightingReport::Unreported:
        break;
    case LaggedLightingReport::NoDedicatedAsyncCompute:
        NWB_LOGGER_ESSENTIAL_INFO(
            NWB_TEXT("RendererSystem: frame-lagged async lighting Graphics queue route accepted (no dedicated AsyncCompute lane, target generation {})"),
            targetGeneration
        );
        break;
    case LaggedLightingReport::BootstrapAccepted:
        NWB_LOGGER_ESSENTIAL_INFO(
            NWB_TEXT("RendererSystem: frame-lagged async lighting bootstrap accepted (target generation {})"),
            targetGeneration
        );
        break;
    case LaggedLightingReport::ActiveHistoryAccepted:
        NWB_LOGGER_ESSENTIAL_INFO(
            NWB_TEXT("RendererSystem: frame-lagged async lighting active history accepted (target generation {})"),
            targetGeneration
        );
        break;
    case LaggedLightingReport::CurrentFrameAccepted:
        NWB_LOGGER_ESSENTIAL_INFO(
            NWB_TEXT("RendererSystem: frame-lagged async lighting current-frame path accepted (target generation {})"),
            targetGeneration
        );
        break;
    }
}

void RendererSystem::resetLaggedLightingHistoryCopyStateHandoffs()noexcept{
    m_laggedLightingHistoryCopyInputStateHandoff.reset();
    m_laggedLightingHistoryCopyStateHandoff.reset();
}

void RendererSystem::invalidateLaggedLightingHistorySubmission()noexcept{
    m_laggedLightingHistorySubmissionToken = Core::QueueSubmissionToken{};
}

void RendererSystem::resetLaggedLightingHistoryTracking()noexcept{
    invalidateLaggedLightingHistorySubmission();
    m_laggedLightingHistoryGeneration = 0u;
}

void RendererSystem::resetTargetGenerationStateHandoffs()noexcept{
    // Replaced targets invalidate retained compute-local state.
    m_shadowComputeBaseStateHandoff.reset();
    m_shadowComputeInputStateHandoff.reset();
    m_shadowComputePersistentStateHandoff.reset();
    m_shadowVisibilityStateHandoff.reset();
    m_shadowVisibilityLightingStateHandoff.reset();
    m_shadowVisibilityReturnStateHandoff.reset();
    m_causticsComputeBaseStateHandoff.reset();
    m_causticsComputeInputStateHandoff.reset();
    m_causticsComputePersistentStateHandoff.reset();
    m_causticsStateHandoff.reset();
    m_causticIrradianceLightingStateHandoff.reset();
    m_causticIrradianceReturnStateHandoff.reset();
    m_surfelGiComputeBaseStateHandoff.reset();
    m_surfelGiComputeInputStateHandoff.reset();
    m_surfelGiComputePersistentStateHandoff.reset();
    m_surfelGiStateHandoff.reset();
    m_surfelIrradianceLightingStateHandoff.reset();
    m_surfelIrradianceReturnStateHandoff.reset();
    m_deferredLightingBaseStateHandoff.reset();
    m_deferredLightingInputStateHandoff.reset();
    m_deferredLightingStateHandoff.reset();
    m_avboitLightingStateHandoff.reset();
    m_avboitCompositeStateHandoff.reset();
    m_opaqueColorCompositeStateHandoff.reset();
    m_deferredCompositeBaseStateHandoff.reset();
    m_deferredCompositeInputStateHandoff.reset();
    m_deferredCompositeStateHandoff.reset();
    m_compositeColorPresentStateHandoff.reset();
    m_deferredPresentBaseStateHandoff.reset();
    m_deferredPresentInputStateHandoff.reset();
    m_deferredPresentStateHandoff.reset();
    resetLaggedLightingHistoryCopyStateHandoffs();
}

void RendererSystem::resetInvalidatedResourceStateHandoffs()noexcept{
    // Full invalidation also drops preparation and prefix handoffs.
    m_shadowPrepareStateHandoff.reset();
    m_meshViewSetupStateHandoff.reset();
    m_sceneShadingSetupStateHandoff.reset();
    m_deferredClearStateHandoff.reset();
    m_frameSetupStateFanInHandoff.reset();
    m_gbufferStateHandoff.reset();
    m_postGbufferNormalizedStateHandoff.reset();
    resetTargetGenerationStateHandoffs();
}

void RendererSystem::resetFrameRecordingStateHandoffs()noexcept{
    // Preserve accepted compute-local state across fresh recordings.
    m_meshViewSetupStateHandoff.reset();
    m_sceneShadingSetupStateHandoff.reset();
    m_deferredClearStateHandoff.reset();
    m_frameSetupStateFanInHandoff.reset();
    m_gbufferStateHandoff.reset();
    m_postGbufferNormalizedStateHandoff.reset();
    m_shadowComputeBaseStateHandoff.reset();
    m_shadowComputeInputStateHandoff.reset();
    m_shadowVisibilityStateHandoff.reset();
    m_shadowVisibilityLightingStateHandoff.reset();
    m_causticsComputeBaseStateHandoff.reset();
    m_causticsComputeInputStateHandoff.reset();
    m_causticsStateHandoff.reset();
    m_causticIrradianceLightingStateHandoff.reset();
    m_surfelGiComputeBaseStateHandoff.reset();
    m_surfelGiComputeInputStateHandoff.reset();
    m_surfelGiStateHandoff.reset();
    m_surfelIrradianceLightingStateHandoff.reset();
    m_deferredLightingBaseStateHandoff.reset();
    m_deferredLightingInputStateHandoff.reset();
    m_deferredLightingStateHandoff.reset();
    m_avboitLightingStateHandoff.reset();
    m_avboitCompositeStateHandoff.reset();
    m_opaqueColorCompositeStateHandoff.reset();
    m_deferredCompositeBaseStateHandoff.reset();
    m_deferredCompositeInputStateHandoff.reset();
    m_deferredCompositeStateHandoff.reset();
    m_compositeColorPresentStateHandoff.reset();
    m_deferredPresentBaseStateHandoff.reset();
    m_deferredPresentInputStateHandoff.reset();
    m_deferredPresentStateHandoff.reset();
    resetLaggedLightingHistoryCopyStateHandoffs();
}

void RendererSystem::resetAbandonedFrameStateHandoffs()noexcept{
    // Rejected frames keep only cross-frame state from earlier accepted producers.
    m_meshViewSetupStateHandoff.reset();
    m_sceneShadingSetupStateHandoff.reset();
    m_deferredClearStateHandoff.reset();
    m_frameSetupStateFanInHandoff.reset();
    m_gbufferStateHandoff.reset();
    m_postGbufferNormalizedStateHandoff.reset();
    m_shadowComputeBaseStateHandoff.reset();
    m_shadowComputeInputStateHandoff.reset();
    m_shadowVisibilityStateHandoff.reset();
    m_shadowVisibilityLightingStateHandoff.reset();
    m_causticsStateHandoff.reset();
    m_causticIrradianceLightingStateHandoff.reset();
    m_surfelGiComputeBaseStateHandoff.reset();
    m_surfelGiComputeInputStateHandoff.reset();
    m_surfelGiStateHandoff.reset();
    m_surfelIrradianceLightingStateHandoff.reset();
    m_deferredLightingBaseStateHandoff.reset();
    m_deferredLightingInputStateHandoff.reset();
    m_deferredLightingStateHandoff.reset();
    m_avboitLightingStateHandoff.reset();
    m_avboitCompositeStateHandoff.reset();
    m_opaqueColorCompositeStateHandoff.reset();
    m_deferredCompositeBaseStateHandoff.reset();
    m_deferredCompositeInputStateHandoff.reset();
    m_deferredCompositeStateHandoff.reset();
    m_compositeColorPresentStateHandoff.reset();
    m_deferredPresentBaseStateHandoff.reset();
    m_deferredPresentInputStateHandoff.reset();
    m_deferredPresentStateHandoff.reset();
    resetLaggedLightingHistoryCopyStateHandoffs();
}

void RendererSystem::resetRejectedShadowVisibilityStateHandoffs()noexcept{
    // Preserve accepted Graphics prefix state when graph-owned shadow visibility rejects.
    m_shadowComputeBaseStateHandoff.reset();
    m_shadowComputeInputStateHandoff.reset();
    m_shadowVisibilityStateHandoff.reset();
    m_shadowVisibilityLightingStateHandoff.reset();
    m_causticsComputeBaseStateHandoff.reset();
    m_causticsComputeInputStateHandoff.reset();
    m_causticsStateHandoff.reset();
    m_causticIrradianceLightingStateHandoff.reset();
    m_surfelGiComputeBaseStateHandoff.reset();
    m_surfelGiComputeInputStateHandoff.reset();
    m_surfelGiStateHandoff.reset();
    m_surfelIrradianceLightingStateHandoff.reset();
    m_deferredLightingBaseStateHandoff.reset();
    m_deferredLightingInputStateHandoff.reset();
    m_deferredLightingStateHandoff.reset();
    m_avboitLightingStateHandoff.reset();
    m_avboitCompositeStateHandoff.reset();
    m_opaqueColorCompositeStateHandoff.reset();
    m_deferredCompositeBaseStateHandoff.reset();
    m_deferredCompositeInputStateHandoff.reset();
    m_deferredCompositeStateHandoff.reset();
    m_compositeColorPresentStateHandoff.reset();
    m_deferredPresentBaseStateHandoff.reset();
    m_deferredPresentInputStateHandoff.reset();
    m_deferredPresentStateHandoff.reset();
}


bool RendererSystem::validateResources(const u32 width, const u32 height, const u32 sampleCount){
    static_cast<void>(sampleCount);
    m_raytracingSystem.logCapabilityOnce();
    if(width == 0 || height == 0)
        return true;

    if(!ensureFrameCommandLists())
        return false;

    if(!prepareGpuTimingScopes())
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: GPU timing scope preparation failed; timing samples may be skipped"));

    DeferredFrameTargets& deferredTargets = m_deferredState.m_targets;
    bool targetsReady = deferredTargets.valid() && deferredTargets.width == width && deferredTargets.height == height;
    if(!targetsReady){
        // New targets invalidate stale compute scratch and visibility returns.
        resetTargetGenerationStateHandoffs();
        resetLaggedLightingHistoryTracking();
        targetsReady = m_deferredSystem.createDeferredFrameTargets(width, height);
    }
    if(!targetsReady)
        return false;

    if(Core::Framebuffer* presentationFramebuffer = m_graphics.getCurrentFramebuffer()){
        if(!m_deferredSystem.createDeferredPresentPipeline(presentationFramebuffer))
            return false;
    }

    if(!m_avboitSystem.createAvboitPipelines())
        return false;

    if(!m_graphics.queryFeatureSupport(Core::Feature::Meshlets)){
        if(!m_materialSystem.createComputeEmulationResources())
            return false;
    }

    if(!m_meshSystem.createMeshViewBuffer())
        return false;

    if(!m_csgSystem.createCsgIntervalPeelResources(deferredTargets, true))
        return false;

    return true;
}

void RendererSystem::invalidateResources(){
    m_preparedCsgFrameState = CsgFrameState{};
    m_preparedCsgFrameStateValid = false;
    m_preparedHasTransparentRenderers = false;
    m_preparedShadowVisibilityReady = false;
    m_graphicsPrefixTaskGraphValid = false;
    m_graphicsPrefixTask = {};
    m_graphicsPrefixTaskGraph.reset();
    m_graphicsPrefixTaskGraphAnalysis.reset();
    m_graphicsPrefixTaskGraphQueueAssignments.reset();
    m_graphicsPrefixCompiledGraph.reset();
    m_graphicsPrefixRecordedGraph.reset(m_graphicsPrefixCompiledGraph);
    m_graphicsPrefixSubmissionTransaction.reset(m_graphicsPrefixCompiledGraph);
    m_laggedLightingHistoryTaskGraphValid = false;
    m_laggedLightingHistoryTask = {};
    m_laggedLightingPresentationCompletion = {};
    m_laggedLightingHistoryTaskGraph.reset();
    m_laggedLightingHistoryTaskGraphAnalysis.reset();
    m_laggedLightingHistoryTaskGraphQueueAssignments.reset();
    m_laggedLightingHistoryCompiledGraph.reset();
    m_laggedLightingHistoryRecordedGraph.reset(m_laggedLightingHistoryCompiledGraph);
    m_laggedLightingHistorySubmissionTransaction.reset(m_laggedLightingHistoryCompiledGraph);
    m_deferredCompositeTaskGraphValid = false;
    m_deferredCompositeTask = {};
    m_deferredCompositeLightingCompletion = {};
    m_deferredCompositeTaskGraph.reset();
    m_deferredCompositeTaskGraphAnalysis.reset();
    m_deferredCompositeTaskGraphQueueAssignments.reset();
    m_deferredCompositeCompiledGraph.reset();
    m_deferredCompositeRecordedGraph.reset(m_deferredCompositeCompiledGraph);
    m_deferredCompositeSubmissionTransaction.reset(m_deferredCompositeCompiledGraph);
    m_deferredPresentTaskGraphValid = false;
    m_deferredPresentTask = {};
    m_deferredPresentCompositeCompletion = {};
    m_deferredPresentSurfelGiCompletion = {};
    m_deferredPresentTaskGraph.reset();
    m_deferredPresentTaskGraphAnalysis.reset();
    m_deferredPresentTaskGraphQueueAssignments.reset();
    m_deferredPresentCompiledGraph.reset();
    m_deferredPresentRecordedGraph.reset(m_deferredPresentCompiledGraph);
    m_deferredPresentSubmissionTransaction.reset(m_deferredPresentCompiledGraph);
    m_shadowVisibilityTaskGraphValid = false;
    m_shadowVisibilityTask = {};
    m_shadowVisibilityPrefixCompletion = {};
    m_shadowVisibilityTaskGraph.reset();
    m_shadowVisibilityTaskGraphAnalysis.reset();
    m_shadowVisibilityTaskGraphQueueAssignments.reset();
    m_shadowVisibilityCompiledGraph.reset();
    m_shadowVisibilityRecordedGraph.reset(m_shadowVisibilityCompiledGraph);
    m_shadowVisibilitySubmissionTransaction.reset(m_shadowVisibilityCompiledGraph);
    m_hardwareCausticsTaskGraphValid = false;
    m_hardwareCausticsTask = {};
    m_hardwareCausticsPrefixCompletion = {};
    m_hardwareCausticsLaggedHistoryCompletion = {};
    m_hardwareCausticsTaskGraph.reset();
    m_hardwareCausticsTaskGraphAnalysis.reset();
    m_hardwareCausticsTaskGraphQueueAssignments.reset();
    m_hardwareCausticsCompiledGraph.reset();
    m_hardwareCausticsRecordedGraph.reset(m_hardwareCausticsCompiledGraph);
    m_hardwareCausticsSubmissionTransaction.reset(m_hardwareCausticsCompiledGraph);
    m_softwareCausticsTaskGraphValid = false;
    m_softwareCausticsTask = {};
    m_softwareCausticsShadowVisibilityCompletion = {};
    m_softwareCausticsTaskGraph.reset();
    m_softwareCausticsTaskGraphAnalysis.reset();
    m_softwareCausticsTaskGraphQueueAssignments.reset();
    m_softwareCausticsCompiledGraph.reset();
    m_softwareCausticsRecordedGraph.reset(m_softwareCausticsCompiledGraph);
    m_softwareCausticsSubmissionTransaction.reset(m_softwareCausticsCompiledGraph);
    m_surfelGiTaskGraphValid = false;
    m_surfelGiTask = {};
    m_surfelGiEffectsCompletion = {};
    m_surfelGiTaskGraph.reset();
    m_surfelGiTaskGraphAnalysis.reset();
    m_surfelGiTaskGraphQueueAssignments.reset();
    m_surfelGiCompiledGraph.reset();
    m_surfelGiRecordedGraph.reset(m_surfelGiCompiledGraph);
    m_surfelGiSubmissionTransaction.reset(m_surfelGiCompiledGraph);
    m_avboitTaskGraphValid = false;
    m_avboitPreTask = {};
    m_avboitDepthWarpTask = {};
    m_avboitExtinctionTask = {};
    m_avboitIntegrationTask = {};
    m_avboitAccumulationTask = {};
    m_avboitPrefixCompletion = {};
    m_avboitTaskGraph.reset();
    m_avboitTaskGraphAnalysis.reset();
    m_avboitTaskGraphQueueAssignments.reset();
    m_avboitCompiledGraph.reset();
    m_avboitRecordedGraph.reset(m_avboitCompiledGraph);
    m_avboitSubmissionTransaction.reset(m_avboitCompiledGraph);
    m_deferredLightingTaskGraphValid = false;
    m_deferredLightingTask = {};
    m_deferredLightingAvboitCompletion = {};
    m_deferredLightingSurfelGiCompletion = {};
    m_deferredLightingHistoryCompletion = {};
    m_deferredLightingTaskGraph.reset();
    m_deferredLightingTaskGraphAnalysis.reset();
    m_deferredLightingTaskGraphQueueAssignments.reset();
    m_deferredLightingCompiledGraph.reset();
    m_deferredLightingRecordedGraph.reset(m_deferredLightingCompiledGraph);
    m_deferredLightingSubmissionTransaction.reset(m_deferredLightingCompiledGraph);
    m_taskGraphDeviceGeneration = m_taskGraphDeviceGeneration == Limit<u16>::s_Max
        ? 1u
        : static_cast<u16>(m_taskGraphDeviceGeneration + 1u)
    ;
    resetInvalidatedResourceStateHandoffs();
    m_meshViewSetupCommandList.reset();
    m_sceneShadingSetupCommandList.reset();
    m_deferredClearCommandList.reset();
    m_gbufferCommandList.reset();
    m_postGbufferNormalizeCommandList.reset();
    m_frameRecoveryCommandList.reset();
    m_shadowPrepareCommandList.reset();
    resetLaggedLightingHistoryTracking();
    m_frameRenderRecoveryFailed = false;
    // Retire heap-retained TLAS resources before releasing their backing state.
    if(m_rayTracingState.m_tlasHeapHandle.valid()){
        auto& device = m_graphics.getDevice();
        device.getDescriptorHeap().free(m_rayTracingState.m_tlasHeapHandle);
    }
    m_raytracingSystem.releaseCausticEmissionTargetHeapHandle();
    m_raytracingSystem.releaseRayTraceMaterialContextHeapHandles();
    m_raytracingSystem.releaseSwBvhScratchHeapHandles();
    m_raytracingSystem.releaseSurfelGiHeapHandles();
    m_deferredSystem.resetDeferredFrameTargets();
    m_meshSystem.releaseAllMeshGeometryHeapHandles();
    m_meshSystem.releaseMeshFrameHeapHandles();
    m_meshState.invalidateResources();
    m_materialSystem.releaseMaterialResourceReferences();
    m_materialState.invalidateResources();
    m_drawState.invalidateResources();
    m_csgSystem.releaseCsgClipContextHeapHandles();
    m_csgState.invalidateResources();
    m_deferredState.invalidateResources();
    m_avboitState.invalidateResources();
    m_rayTracingState.invalidateResources();
}

void RendererSystem::update(Core::ECS::World& world, f32 delta){
    static_cast<void>(world);
    static_cast<void>(delta);
}

bool RendererSystem::ensureFrameCommandLists(){
    auto& device = m_graphics.getDevice();

    if(!m_meshViewSetupCommandList){
        m_meshViewSetupCommandList = device.createCommandList();
        if(!m_meshViewSetupCommandList){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create mesh-view setup command list"));
            return false;
        }
    }

    if(!m_sceneShadingSetupCommandList){
        m_sceneShadingSetupCommandList = device.createCommandList();
        if(!m_sceneShadingSetupCommandList){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create scene-shading setup command list"));
            return false;
        }
    }

    if(!m_deferredClearCommandList){
        m_deferredClearCommandList = device.createCommandList();
        if(!m_deferredClearCommandList){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred-clear command list"));
            return false;
        }
    }

    if(!m_gbufferCommandList){
        m_gbufferCommandList = device.createCommandList();
        if(!m_gbufferCommandList){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create G-buffer command list"));
            return false;
        }
    }

    if(!m_postGbufferNormalizeCommandList){
        m_postGbufferNormalizeCommandList = device.createCommandList();
        if(!m_postGbufferNormalizeCommandList){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create post-G-buffer normalization command list"));
            return false;
        }
    }

    if(!m_frameRecoveryCommandList){
        m_frameRecoveryCommandList = device.createCommandList();
        if(!m_frameRecoveryCommandList){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create frame recovery command list"));
            return false;
        }
    }

    if(!m_shadowPrepareCommandList){
        m_shadowPrepareCommandList = device.createCommandList();
        if(!m_shadowPrepareCommandList){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow preparation command list"));
            return false;
        }
    }

    return true;
}

bool RendererSystem::prepareGpuTimingScopes(){
    auto& device = m_graphics.getDevice();
    // A timestamp range consumes a begin/end query pair. High-frequency raster/mesh scopes may emit many ranges;
    // the sort and interval-clear scopes each emit two ranges.
    static constexpr u32 s_GpuTimingQueriesPerRange = 2u;
    static constexpr u32 s_GpuTimingQueriesPerTwoRangeScope = 2u * s_GpuTimingQueriesPerRange;
    static constexpr u32 s_GpuTimingHighFrequencyScopeQueryBudget = 128u;

    struct ScopeReservation{
        const Core::GpuTimingScopeDefinition* scope;
        u32 queryCount;
    };
    const ScopeReservation scopeReservations[] = {
        { &RendererGpuTimingScope::s_MeshDispatch, s_GpuTimingHighFrequencyScopeQueryBudget },
        { &RendererGpuTimingScope::s_Raster, s_GpuTimingHighFrequencyScopeQueryBudget },
        { &RendererGpuTimingScope::s_Frame, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_AsyncPrefix, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_AsyncShadow, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_AsyncSurfelGi, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_AsyncFinal, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_DeferredClear, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_ShadowVisibility, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_ShadowOpaqueTrace, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_ShadowGeometryDownsample, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_ShadowOpaqueTemporal, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_ShadowOpaqueResolve, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_ShadowTransparentTrace, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_ShadowTransparentTemporal, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_ShadowTransparentResolve, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_SwBvhSort, s_GpuTimingQueriesPerTwoRangeScope },
        { &RendererGpuTimingScope::s_CausticPhotons, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_CausticResolve, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_DeferredLighting, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_DeferredComposite, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_DeferredPresent, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_MaterialUpload, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_OpaqueRegular, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_OpaqueCsgReceiverSurface, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_OpaqueCsg, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_CsgUpload, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_CsgSampleStateUpload, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_CsgIntervalClear, s_GpuTimingQueriesPerTwoRangeScope },
        { &RendererGpuTimingScope::s_CsgIntervalPeel, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_CsgReceiverSpanBuild, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_CsgIntervalCombine, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_CsgCapFill, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_TransparentCsgIntervals, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_AvboitClear, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_AvboitOccupancy, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_AvboitDepthWarp, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_AvboitExtinction, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_AvboitIntegration, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_AvboitAccumulate, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_SurfelSpawn, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_SurfelAgeFree, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_SurfelHashBuild, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_SurfelTrace, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_SurfelResolve, s_GpuTimingQueriesPerRange },
        { &RendererGpuTimingScope::s_SurfelUpsample, s_GpuTimingQueriesPerRange },
    };

    for(const ScopeReservation& reservation : scopeReservations){
        if(!m_graphics.gpuTiming().prepareScopeQueries(reservation.scope->identity, device, reservation.queryCount)){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: failed to prepare GPU timing scope '{}'"), StringConvert(reservation.scope->identity.c_str()));
            return false;
        }
    }

    return true;
}

bool RendererSystem::prepareResources(Core::Framebuffer* framebuffer){
    m_preparedShadowVisibilityReady = false;
    m_preparedHasTransparentRenderers = false;

    if(!framebuffer)
        return false;

    m_meshSystem.pruneRuntimeMeshResources();
    m_preparedHasTransparentRenderers = m_materialSystem.prepareVisibleMaterialSurfaceInfos();
    m_materialSystem.prepareVisibleMaterialInstanceMutableCache();
    m_preparedCsgFrameState = CsgFrameState{};
    m_preparedCsgFrameStateValid = false;

    if(!m_deferredState.m_targets.valid())
        return true;
    DeferredFrameTargets& deferredTargets = m_deferredState.m_targets;

    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_PrepareArena);
    m_preparedCsgFrameState = HasCsgFrameCandidates(m_world)
        ? m_csgSystem.buildFrameState(scratchArena)
        : CsgFrameState{}
    ;
    m_preparedCsgFrameStateValid = true;
    const bool hasCsgFrameWork = !m_preparedCsgFrameState.empty();
    if(hasCsgFrameWork && !deferredTargets.csgIntervalTargetsValid())
        return false;

    // CSG receiver ranges are addressed by material-pass instance index.  A CSG-active material pass reserves one
    // range for every compatible renderer, not only the renderers that receive CSG clipping, so the complete
    // renderer view is the safe prepass capacity bound for either material pass.
    const usize csgReceiverRangeCount = hasCsgFrameWork
        ? m_world.view<RendererComponent>().candidateCount()
        : 0u
    ;

    if(!m_materialSystem.prepareMaterialPassResources(
        deferredTargets.framebuffer.get(),
        MaterialPipelinePass::Opaque,
        false,
        m_preparedCsgFrameState,
        nullptr
    ))
        return false;

    if(
        m_preparedHasTransparentRenderers
        && !m_avboitSystem.prepareAvboitPassResources(deferredTargets, m_preparedCsgFrameState)
    )
        return false;

    // Refresh mesh descriptors after transparent preparation can grow shared buffers.
    if(
        m_preparedHasTransparentRenderers
        && !m_materialSystem.prepareMaterialPassResources(
            deferredTargets.framebuffer.get(),
            MaterialPipelinePass::Opaque,
            false,
            m_preparedCsgFrameState,
            nullptr
        )
    )
        return false;

    // All material passes have now established their shared frame buffers.  Create CSG resources once from this
    // renderer-owned prepass so draw paths only consume the prepared layouts and handles.
    if(
        hasCsgFrameWork
        && !m_csgSystem.prepareCsgFrameResources(
            csgReceiverRangeCount,
            static_cast<usize>(m_preparedCsgFrameState.cutterCount)
        )
    )
        return false;

    NWB_ASSERT(m_meshViewSetupCommandList);
    NWB_ASSERT(m_sceneShadingSetupCommandList);
    NWB_ASSERT(m_deferredClearCommandList);
    NWB_ASSERT(m_gbufferCommandList);
    NWB_ASSERT(m_postGbufferNormalizeCommandList);
    NWB_ASSERT(m_shadowPrepareCommandList);

    auto& device = m_graphics.getDevice();

    // Preparation uploads target slots and initializes surfels on the Graphics lane when no dedicated Compute lane
    // is available.
    const bool deferredBindlessSlotsWereUploaded = deferredTargets.bindless.slotsUploaded;
    m_raytracingSystem.discardSurfelResourceInitialization();
    Core::GpuTimingSubmissionTicket shadowPrepareTimingTicket(m_graphics.gpuTiming());
    const auto discardShadowPrepare = [&](){
        // Failed preparation forces a safe cache rebuild; a previous key may name retired storage.
        m_rayTracingState.m_tlasStaticSceneHashValid = false;
        m_rayTracingState.m_sceneSwBvhStaticSceneHashValid = false;
        m_rayTracingState.m_hwShadowMaterialContextHashValid = false;
        m_rayTracingState.m_swShadowMaterialContextHashValid = false;
        shadowPrepareTimingTicket.discard();
        m_shadowPrepareStateHandoff.reset();
        m_preparedShadowVisibilityReady = false;
        deferredTargets.bindless.slotsUploaded = deferredBindlessSlotsWereUploaded;
        m_raytracingSystem.discardSurfelResourceInitialization();
    };
    bool shadowPrepareRecorded = false;
    const Core::Graphics::JobHandle shadowPrepareJob = m_graphics.scheduleGraphicsJob([
        this,
        &deferredTargets,
        &shadowPrepareRecorded,
        &shadowPrepareTimingTicket
    ](){
        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(shadowPrepareTimingTicket);
        shadowPrepareRecorded = recordShadowPrepareCommandList(deferredTargets);
    });
    if(!shadowPrepareJob.valid()){
        discardShadowPrepare();
        return false;
    }

    m_graphics.waitJob(shadowPrepareJob);
    if(!shadowPrepareRecorded){
        discardShadowPrepare();
        return false;
    }

    Core::CommandList* shadowPrepareCommandLists[] = { m_shadowPrepareCommandList.get() };
    if(!shadowPrepareTimingTicket.submit(device, shadowPrepareCommandLists, 1u)){
        discardShadowPrepare();
        return false;
    }
    m_raytracingSystem.finalizeSurfelResourceInitialization();

    return true;
}

bool RendererSystem::recordShadowPrepareCommandList(DeferredFrameTargets& deferredTargets){
    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_PrepareArena);

    m_shadowPrepareCommandList->open();
    const bool deferredBindlessResourcesUploaded = m_deferredSystem.uploadDeferredBindlessFrameResources(
        *m_shadowPrepareCommandList,
        deferredTargets
    );
    const bool shadowResourcesPrepared = deferredBindlessResourcesUploaded
        && m_raytracingSystem.prepareShadowVisibilityResources(
            *m_shadowPrepareCommandList,
            deferredTargets,
            scratchArena,
            m_preparedShadowVisibilityReady
        )
    ;
    // Upload final slot indirection after all capacity growth settles.
    const bool traceMaterialContextUploaded = shadowResourcesPrepared
        && m_raytracingSystem.uploadRayTraceMaterialContextSlots(*m_shadowPrepareCommandList)
    ;
    // Preserve preparation output state for the first render list.
    m_shadowPrepareCommandList->close(&m_shadowPrepareStateHandoff);
    return traceMaterialContextUploaded && m_shadowPrepareStateHandoff.valid();
}

void RendererSystem::render(Core::Framebuffer* framebuffer){
    m_graphicsPrefixTaskGraphValid = false;
    m_graphicsPrefixTask = {};
    m_graphicsPrefixTaskGraph.reset();
    m_graphicsPrefixTaskGraphAnalysis.reset();
    m_graphicsPrefixTaskGraphQueueAssignments.reset();
    m_graphicsPrefixCompiledGraph.reset();
    m_graphicsPrefixRecordedGraph.reset(m_graphicsPrefixCompiledGraph);
    m_graphicsPrefixSubmissionTransaction.reset(m_graphicsPrefixCompiledGraph);
    m_laggedLightingHistoryTaskGraphValid = false;
    m_laggedLightingHistoryTask = {};
    m_laggedLightingPresentationCompletion = {};
    m_laggedLightingHistoryTaskGraph.reset();
    m_laggedLightingHistoryTaskGraphAnalysis.reset();
    m_laggedLightingHistoryTaskGraphQueueAssignments.reset();
    m_laggedLightingHistoryCompiledGraph.reset();
    m_laggedLightingHistoryRecordedGraph.reset(m_laggedLightingHistoryCompiledGraph);
    m_laggedLightingHistorySubmissionTransaction.reset(m_laggedLightingHistoryCompiledGraph);
    m_deferredCompositeTaskGraphValid = false;
    m_deferredCompositeTask = {};
    m_deferredCompositeLightingCompletion = {};
    m_deferredCompositeTaskGraph.reset();
    m_deferredCompositeTaskGraphAnalysis.reset();
    m_deferredCompositeTaskGraphQueueAssignments.reset();
    m_deferredCompositeCompiledGraph.reset();
    m_deferredCompositeRecordedGraph.reset(m_deferredCompositeCompiledGraph);
    m_deferredCompositeSubmissionTransaction.reset(m_deferredCompositeCompiledGraph);
    m_deferredPresentTaskGraphValid = false;
    m_deferredPresentTask = {};
    m_deferredPresentCompositeCompletion = {};
    m_deferredPresentSurfelGiCompletion = {};
    m_deferredPresentTaskGraph.reset();
    m_deferredPresentTaskGraphAnalysis.reset();
    m_deferredPresentTaskGraphQueueAssignments.reset();
    m_deferredPresentCompiledGraph.reset();
    m_deferredPresentRecordedGraph.reset(m_deferredPresentCompiledGraph);
    m_deferredPresentSubmissionTransaction.reset(m_deferredPresentCompiledGraph);
    m_shadowVisibilityTaskGraphValid = false;
    m_shadowVisibilityTask = {};
    m_shadowVisibilityPrefixCompletion = {};
    m_shadowVisibilityTaskGraph.reset();
    m_shadowVisibilityTaskGraphAnalysis.reset();
    m_shadowVisibilityTaskGraphQueueAssignments.reset();
    m_shadowVisibilityCompiledGraph.reset();
    m_shadowVisibilityRecordedGraph.reset(m_shadowVisibilityCompiledGraph);
    m_shadowVisibilitySubmissionTransaction.reset(m_shadowVisibilityCompiledGraph);
    m_hardwareCausticsTaskGraphValid = false;
    m_hardwareCausticsTask = {};
    m_hardwareCausticsPrefixCompletion = {};
    m_hardwareCausticsLaggedHistoryCompletion = {};
    m_hardwareCausticsTaskGraph.reset();
    m_hardwareCausticsTaskGraphAnalysis.reset();
    m_hardwareCausticsTaskGraphQueueAssignments.reset();
    m_hardwareCausticsCompiledGraph.reset();
    m_hardwareCausticsRecordedGraph.reset(m_hardwareCausticsCompiledGraph);
    m_hardwareCausticsSubmissionTransaction.reset(m_hardwareCausticsCompiledGraph);
    m_softwareCausticsTaskGraphValid = false;
    m_softwareCausticsTask = {};
    m_softwareCausticsShadowVisibilityCompletion = {};
    m_softwareCausticsTaskGraph.reset();
    m_softwareCausticsTaskGraphAnalysis.reset();
    m_softwareCausticsTaskGraphQueueAssignments.reset();
    m_softwareCausticsCompiledGraph.reset();
    m_softwareCausticsRecordedGraph.reset(m_softwareCausticsCompiledGraph);
    m_softwareCausticsSubmissionTransaction.reset(m_softwareCausticsCompiledGraph);
    m_surfelGiTaskGraphValid = false;
    m_surfelGiTask = {};
    m_surfelGiEffectsCompletion = {};
    m_surfelGiTaskGraph.reset();
    m_surfelGiTaskGraphAnalysis.reset();
    m_surfelGiTaskGraphQueueAssignments.reset();
    m_surfelGiCompiledGraph.reset();
    m_surfelGiRecordedGraph.reset(m_surfelGiCompiledGraph);
    m_surfelGiSubmissionTransaction.reset(m_surfelGiCompiledGraph);
    m_avboitTaskGraphValid = false;
    m_avboitPreTask = {};
    m_avboitDepthWarpTask = {};
    m_avboitExtinctionTask = {};
    m_avboitIntegrationTask = {};
    m_avboitAccumulationTask = {};
    m_avboitPrefixCompletion = {};
    m_avboitTaskGraph.reset();
    m_avboitTaskGraphAnalysis.reset();
    m_avboitTaskGraphQueueAssignments.reset();
    m_avboitCompiledGraph.reset();
    m_avboitRecordedGraph.reset(m_avboitCompiledGraph);
    m_avboitSubmissionTransaction.reset(m_avboitCompiledGraph);
    m_deferredLightingTaskGraphValid = false;
    m_deferredLightingTask = {};
    m_deferredLightingAvboitCompletion = {};
    m_deferredLightingSurfelGiCompletion = {};
    m_deferredLightingHistoryCompletion = {};
    m_deferredLightingTaskGraph.reset();
    m_deferredLightingTaskGraphAnalysis.reset();
    m_deferredLightingTaskGraphQueueAssignments.reset();
    m_deferredLightingCompiledGraph.reset();
    m_deferredLightingRecordedGraph.reset(m_deferredLightingCompiledGraph);
    m_deferredLightingSubmissionTransaction.reset(m_deferredLightingCompiledGraph);

    if(!framebuffer)
        return;

    if(!m_deferredState.m_targets.valid())
        return;
    DeferredFrameTargets& deferredTargets = m_deferredState.m_targets;

    NWB_ASSERT(m_preparedCsgFrameStateValid);
    NWB_ASSERT(m_shadowPrepareStateHandoff.valid());
    NWB_ASSERT(deferredTargets.bindless.slotsUploaded);

    const CsgFrameState csgFrameState = m_preparedCsgFrameState;
    const bool hasOpaqueCsgFrameWork = csgFrameState.hasOpaqueStaticWork || csgFrameState.hasOpaqueSkinnedWork;
    const bool hasTransparentRenderers = m_preparedHasTransparentRenderers;
    NWB_ASSERT(csgFrameState.empty() || deferredTargets.csgIntervalTargetsValid());
    auto& device = m_graphics.getDevice();
    if(m_graphics.isDeviceRecreationRequested() || device.isDeviceLost()){
        if(device.isDeviceLost())
            m_graphics.requestDeviceRecreation();
        return;
    }
    if(m_frameRenderRecoveryFailed){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: frame render recovery failed; rendering is suspended until resources are recreated"));
        return;
    }
    const bool dedicatedAsyncCompute = device.isRenderLaneDedicated(Core::RenderLane::AsyncCompute);
    const bool laggedAsyncLightingRequested = m_frameLaggedAsyncLightingEnabled && dedicatedAsyncCompute;
    const bool laggedLightingHistoryResourcesReady = deferredTargets.laggedLightingHistory.valid();
    if(laggedAsyncLightingRequested && !laggedLightingHistoryResourcesReady){
        NWB_ASSERT(laggedLightingHistoryResourcesReady);
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: lagged async lighting requires validated history targets"));
        return;
    }

    if(laggedAsyncLightingRequested){
        const u64 historyGeneration = deferredTargets.laggedLightingHistory.generation;
        if(m_laggedLightingHistoryGeneration != historyGeneration){
            // Target generations prevent history from using recycled slots after resize.
            invalidateLaggedLightingHistorySubmission();
            resetLaggedLightingHistoryCopyStateHandoffs();
            m_laggedLightingHistoryGeneration = historyGeneration;
        }
    }
    else{
        resetLaggedLightingHistoryTracking();
    }
    const bool hardwareShadowSupported =
        m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct)
        && m_graphics.queryFeatureSupport(Core::Feature::RayQuery)
    ;
    const bool laggedAsyncLightingSchedule =
        laggedAsyncLightingRequested
        && laggedLightingHistoryResourcesReady
        && m_laggedLightingHistorySubmissionToken.valid()
    ;
    // History capture is graph-owned and remains available whenever the opt-in path has a distinct compute
    // transport.
    const bool captureLaggedLightingHistory = laggedAsyncLightingRequested;
    // Software caustics are graph-owned. A distinct Compute family is the only route that may resolve to Compute;
    // otherwise the compiler routes the same task through Graphics without a renderer fallback topology.
    const bool shadowVisibilityExpectedCompute = dedicatedAsyncCompute;
    const bool softwareCausticsExpectedCompute = dedicatedAsyncCompute && !hardwareShadowSupported;
    const bool shadowVisibilityPrepared = m_preparedShadowVisibilityReady;
    // Compile every graph before native recording. The prefix imports its established command-list bundle, while
    // all later work records directly through its own graph task.
    const ECSRenderDetail::GpuTaskGraphFrameScheduleInput taskGraphInput{
        .dedicatedAsyncCompute = dedicatedAsyncCompute,
        .frameLaggedAsyncLightingEnabled = m_frameLaggedAsyncLightingEnabled,
        .laggedLightingHistoryReady = laggedLightingHistoryResourcesReady,
        .laggedLightingHistoryAccepted = m_laggedLightingHistorySubmissionToken.valid(),
        .hasTransparentRenderers = hasTransparentRenderers,
        .hardwareCaustics = hardwareShadowSupported,
    };
    buildLaggedLightingHistoryTaskGraph(taskGraphInput, deferredTargets);
    buildGraphicsPrefixTaskGraph(taskGraphInput, deferredTargets);
    const Core::GpuSubmissionPacketId graphicsPrefixPacket = m_graphicsPrefixCompiledGraph.packetForTask(
        m_graphicsPrefixTask
    );
    const Core::GpuPhysicalQueueInfo* const graphicsPrefixQueue = graphicsPrefixPacket.valid()
        ? m_graphicsPrefixCompiledGraph.queueInfo(
            m_graphicsPrefixCompiledGraph.packet(graphicsPrefixPacket).queue
        )
        : nullptr
    ;
    if(
        !m_graphicsPrefixTaskGraphValid
        || !m_graphicsPrefixTask.valid()
        || !graphicsPrefixQueue
        || graphicsPrefixQueue->queueClass != Core::CommandQueue::Graphics
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: graph-owned graphics prefix was unavailable"));
        return;
    }
    Core::CommandList* meshViewSetupCommandList = m_meshViewSetupCommandList.get();
    Core::CommandList* sceneShadingSetupCommandList = m_sceneShadingSetupCommandList.get();
    Core::CommandList* deferredClearCommandList = m_deferredClearCommandList.get();
    Core::CommandList* gbufferCommandList = m_gbufferCommandList.get();
    Core::CommandList* postGbufferNormalizeCommandList = m_postGbufferNormalizeCommandList.get();
    Core::CommandList* frameRecoveryCommandList = m_frameRecoveryCommandList.get();
    NWB_ASSERT(meshViewSetupCommandList);
    NWB_ASSERT(sceneShadingSetupCommandList);
    NWB_ASSERT(deferredClearCommandList);
    NWB_ASSERT(gbufferCommandList);
    NWB_ASSERT(postGbufferNormalizeCommandList);
    NWB_ASSERT(frameRecoveryCommandList);

    resetFrameRecordingStateHandoffs();
    m_raytracingSystem.discardSoftShadowTemporalHistory();

    // Preserve CPU mirrors so rejected recordings can be retried exactly.
    struct PostGbufferPacketCpuState{
        u64 swShadowEdgeStatsPendingSubmissionID = 0u;
        u64 surfelCountReadbackPendingSubmissionID = 0u;

        u32 softShadowFrameIndex = 0u;
        u32 swShadowEdgeStatsTick = 0u;
        u32 swShadowEdgeStatsPendingTick = 0u;
        u32 causticTemporalReuseFrameCount = 0u;
        u32 swCausticFrameIndex = 0u;
        u32 hwCausticFrameIndex = 0u;
        u32 surfelFrameIndex = 0u;
        u32 surfelCountReadbackFrame = 0u;

        bool avboitTargetsNeedClear = true;
        bool deferredBindlessSlotsUploaded = false;
        bool swShadowEdgeStatsPending = false;
        bool swShadowEdgeStatsPendingSubmissionUnconfirmed = false;
        bool swShadowDispatchLogged = false;
        bool causticAccumulatorInitialized = false;
        bool swCausticDispatchLogged = false;
        bool hwCausticDispatchLogged = false;
        bool causticEmissionGateLogged = false;
        bool surfelSeeded = false;
        bool surfelCountReadbackPending = false;
        bool surfelCountReadbackPendingSubmissionUnconfirmed = false;
        Core::CommandQueue::Enum swShadowEdgeStatsPendingSubmissionQueue = Core::CommandQueue::kCount;
        Core::CommandQueue::Enum surfelCountReadbackPendingSubmissionQueue = Core::CommandQueue::kCount;
    };
    const PostGbufferPacketCpuState postGbufferPacketCpuState{
        m_rayTracingState.m_swShadowEdgeStatsPendingSubmissionID,
        m_rayTracingState.m_surfelCountReadbackPendingSubmissionID,
        m_rayTracingState.m_softShadowFrameIndex,
        m_rayTracingState.m_swShadowEdgeStatsTick,
        m_rayTracingState.m_swShadowEdgeStatsPendingTick,
        m_rayTracingState.m_causticTemporalReuseFrameCount,
        m_rayTracingState.m_swCausticFrameIndex,
        m_rayTracingState.m_hwCausticFrameIndex,
        m_rayTracingState.m_surfelFrameIndex,
        m_rayTracingState.m_surfelCountReadbackFrame,
        m_avboitState.m_targetsNeedClear,
        deferredTargets.bindless.slotsUploaded,
        m_rayTracingState.m_swShadowEdgeStatsPending,
        m_rayTracingState.m_swShadowEdgeStatsPendingSubmissionUnconfirmed,
        m_rayTracingState.m_swShadowDispatchLogged,
        m_rayTracingState.m_causticAccumulatorInitialized,
        m_rayTracingState.m_swCausticDispatchLogged,
        m_rayTracingState.m_hwCausticDispatchLogged,
        m_rayTracingState.m_causticEmissionGateLogged,
        m_rayTracingState.m_surfelSeeded,
        m_rayTracingState.m_surfelCountReadbackPending,
        m_rayTracingState.m_surfelCountReadbackPendingSubmissionUnconfirmed,
        m_rayTracingState.m_swShadowEdgeStatsPendingSubmissionQueue,
        m_rayTracingState.m_surfelCountReadbackPendingSubmissionQueue,
    };
    const auto restorePrefixCpuState = [&](){
        // Rejected G-buffer recording invalidates CPU upload mirrors.
        m_drawState.m_meshViewGpuDataValid = false;
        m_deferredState.m_sceneShadingGpuDataValid = false;
        m_deferredState.m_lightGpuDataValid = false;
    };

    const auto restoreShadowCpuState = [&](){
        m_rayTracingState.m_softShadowFrameIndex = postGbufferPacketCpuState.softShadowFrameIndex;
        m_rayTracingState.m_swShadowEdgeStatsTick = postGbufferPacketCpuState.swShadowEdgeStatsTick;
        m_rayTracingState.m_swShadowEdgeStatsPending = postGbufferPacketCpuState.swShadowEdgeStatsPending;
        m_rayTracingState.m_swShadowEdgeStatsPendingTick = postGbufferPacketCpuState.swShadowEdgeStatsPendingTick;
        m_rayTracingState.m_swShadowEdgeStatsPendingSubmissionID = postGbufferPacketCpuState.swShadowEdgeStatsPendingSubmissionID;
        m_rayTracingState.m_swShadowEdgeStatsPendingSubmissionQueue = postGbufferPacketCpuState.swShadowEdgeStatsPendingSubmissionQueue;
        m_rayTracingState.m_swShadowEdgeStatsPendingSubmissionUnconfirmed = postGbufferPacketCpuState.swShadowEdgeStatsPendingSubmissionUnconfirmed;
        m_rayTracingState.m_swShadowDispatchLogged = postGbufferPacketCpuState.swShadowDispatchLogged;
    };

    const auto restoreCausticsCpuState = [&](){
        m_rayTracingState.m_causticAccumulatorInitialized = postGbufferPacketCpuState.causticAccumulatorInitialized;
        m_rayTracingState.m_causticTemporalReuseFrameCount = postGbufferPacketCpuState.causticTemporalReuseFrameCount;
        m_rayTracingState.m_swCausticFrameIndex = postGbufferPacketCpuState.swCausticFrameIndex;
        m_rayTracingState.m_hwCausticFrameIndex = postGbufferPacketCpuState.hwCausticFrameIndex;
        m_rayTracingState.m_swCausticDispatchLogged = postGbufferPacketCpuState.swCausticDispatchLogged;
        m_rayTracingState.m_hwCausticDispatchLogged = postGbufferPacketCpuState.hwCausticDispatchLogged;
        m_rayTracingState.m_causticEmissionGateLogged = postGbufferPacketCpuState.causticEmissionGateLogged;
    };
    const auto restoreSurfelGiCpuState = [&](){
        m_rayTracingState.m_surfelFrameIndex = postGbufferPacketCpuState.surfelFrameIndex;
        m_rayTracingState.m_surfelSeeded = postGbufferPacketCpuState.surfelSeeded;
        m_rayTracingState.m_surfelCountReadbackPending = postGbufferPacketCpuState.surfelCountReadbackPending;
        m_rayTracingState.m_surfelCountReadbackFrame = postGbufferPacketCpuState.surfelCountReadbackFrame;
        m_rayTracingState.m_surfelCountReadbackPendingSubmissionID = postGbufferPacketCpuState.surfelCountReadbackPendingSubmissionID;
        m_rayTracingState.m_surfelCountReadbackPendingSubmissionQueue = postGbufferPacketCpuState.surfelCountReadbackPendingSubmissionQueue;
        m_rayTracingState.m_surfelCountReadbackPendingSubmissionUnconfirmed = postGbufferPacketCpuState.surfelCountReadbackPendingSubmissionUnconfirmed;
    };
    const auto restoreAvboitCpuState = [&](){
        m_avboitState.m_targetsNeedClear = postGbufferPacketCpuState.avboitTargetsNeedClear;
    };
    const auto restorePostGbufferEffectsCpuState = [&](){
        restoreCausticsCpuState();
        restoreSurfelGiCpuState();
        restoreAvboitCpuState();
    };
    const auto restorePostGbufferPacketCpuState = [&](){
        deferredTargets.bindless.slotsUploaded = postGbufferPacketCpuState.deferredBindlessSlotsUploaded;
        restorePrefixCpuState();
        restoreShadowCpuState();
        restorePostGbufferEffectsCpuState();
    };

    // The graph-owned prefix packet retains the established timing scope across its imported command-list bundle.
    Core::GpuTimingSubmissionTicket graphicsPrefixTimingTicket(m_graphics.gpuTiming());
    Core::GpuTimingSubmissionTicket shadowVisibilityTimingTicket(m_graphics.gpuTiming());
    buildShadowVisibilityTaskGraph(
        taskGraphInput,
        deferredTargets,
        shadowVisibilityPrepared,
        hardwareShadowSupported,
        shadowVisibilityTimingTicket
    );
    const Core::GpuSubmissionPacketId shadowVisibilityPacket = m_shadowVisibilityCompiledGraph.packetForTask(
        m_shadowVisibilityTask
    );
    const Core::GpuPhysicalQueueInfo* const shadowVisibilityQueue = shadowVisibilityPacket.valid()
        ? m_shadowVisibilityCompiledGraph.queueInfo(
            m_shadowVisibilityCompiledGraph.packet(shadowVisibilityPacket).queue
        )
        : nullptr
    ;
    if(
        !m_shadowVisibilityTaskGraphValid
        || !m_shadowVisibilityTask.valid()
        || !m_shadowVisibilityPrefixCompletion.valid()
        || !shadowVisibilityQueue
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: graph-owned shadow visibility was unavailable"));
        shadowVisibilityTimingTicket.discard();
        graphicsPrefixTimingTicket.discard();
        return;
    }
    const bool shadowVisibilityRunsOnCompute = shadowVisibilityQueue->queueClass == Core::CommandQueue::Compute;
    if(shadowVisibilityRunsOnCompute != shadowVisibilityExpectedCompute){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: shadow-visibility graph queue disagrees with renderer topology"));
        shadowVisibilityTimingTicket.discard();
        graphicsPrefixTimingTicket.discard();
        return;
    }
    Core::GpuTimingSubmissionTicket hardwareCausticsTimingTicket(m_graphics.gpuTiming());
    if(hardwareShadowSupported){
        buildHardwareCausticsTaskGraph(
            taskGraphInput,
            deferredTargets,
            shadowVisibilityPrepared,
            laggedAsyncLightingSchedule,
            hardwareCausticsTimingTicket
        );
    }
    const Core::GpuSubmissionPacketId hardwareCausticsPacket = m_hardwareCausticsCompiledGraph.packetForTask(
        m_hardwareCausticsTask
    );
    const Core::GpuPhysicalQueueInfo* const hardwareCausticsQueue = hardwareCausticsPacket.valid()
        ? m_hardwareCausticsCompiledGraph.queueInfo(
            m_hardwareCausticsCompiledGraph.packet(hardwareCausticsPacket).queue
        )
        : nullptr
    ;
    if(
        hardwareShadowSupported
        && (
            !m_hardwareCausticsTaskGraphValid
            || !m_hardwareCausticsTask.valid()
            || !m_hardwareCausticsPrefixCompletion.valid()
            || (laggedAsyncLightingSchedule && !m_hardwareCausticsLaggedHistoryCompletion.valid())
            || !hardwareCausticsQueue
        )
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: graph-owned hardware caustics were unavailable"));
        hardwareCausticsTimingTicket.discard();
        shadowVisibilityTimingTicket.discard();
        graphicsPrefixTimingTicket.discard();
        return;
    }
    if(
        hardwareShadowSupported
        && hardwareCausticsQueue->queueClass != Core::CommandQueue::Graphics
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: hardware-caustics graph queue disagrees with renderer topology"));
        hardwareCausticsTimingTicket.discard();
        shadowVisibilityTimingTicket.discard();
        graphicsPrefixTimingTicket.discard();
        return;
    }
    Core::GpuTimingSubmissionTicket softwareCausticsTimingTicket(m_graphics.gpuTiming());
    buildSoftwareCausticsTaskGraph(
        taskGraphInput,
        deferredTargets,
        shadowVisibilityPrepared,
        softwareCausticsTimingTicket
    );
    const Core::GpuSubmissionPacketId softwareCausticsPacket = m_softwareCausticsCompiledGraph.packetForTask(
        m_softwareCausticsTask
    );
    const Core::GpuPhysicalQueueInfo* const softwareCausticsQueue = softwareCausticsPacket.valid()
        ? m_softwareCausticsCompiledGraph.queueInfo(
            m_softwareCausticsCompiledGraph.packet(softwareCausticsPacket).queue
        )
        : nullptr
    ;
    if(
        !hardwareShadowSupported
        && (
            !m_softwareCausticsTaskGraphValid
            || !m_softwareCausticsTask.valid()
            || !m_softwareCausticsShadowVisibilityCompletion.valid()
            || !softwareCausticsQueue
        )
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: graph-owned software caustics were unavailable"));
        softwareCausticsTimingTicket.discard();
        hardwareCausticsTimingTicket.discard();
        shadowVisibilityTimingTicket.discard();
        graphicsPrefixTimingTicket.discard();
        return;
    }
    const bool softwareCausticsRunsOnCompute =
        !hardwareShadowSupported
        && softwareCausticsQueue->queueClass == Core::CommandQueue::Compute
    ;
    if(!hardwareShadowSupported && softwareCausticsRunsOnCompute != softwareCausticsExpectedCompute){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: software-caustics graph queue disagrees with renderer topology"));
        softwareCausticsTimingTicket.discard();
        hardwareCausticsTimingTicket.discard();
        shadowVisibilityTimingTicket.discard();
        graphicsPrefixTimingTicket.discard();
        return;
    }
    Core::GpuTimingSubmissionTicket surfelGiTimingTicket(m_graphics.gpuTiming());
    buildSurfelGiTaskGraph(taskGraphInput, deferredTargets, surfelGiTimingTicket);
    const Core::GpuSubmissionPacketId surfelGiPacket = m_surfelGiCompiledGraph.packetForTask(m_surfelGiTask);
    const Core::GpuPhysicalQueueInfo* const surfelGiQueue = surfelGiPacket.valid()
        ? m_surfelGiCompiledGraph.queueInfo(m_surfelGiCompiledGraph.packet(surfelGiPacket).queue)
        : nullptr
    ;
    if(!m_surfelGiTaskGraphValid || !m_surfelGiEffectsCompletion.valid() || !surfelGiQueue){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: graph-owned surfel GI was unavailable"));
        surfelGiTimingTicket.discard();
        softwareCausticsTimingTicket.discard();
        hardwareCausticsTimingTicket.discard();
        shadowVisibilityTimingTicket.discard();
        graphicsPrefixTimingTicket.discard();
        return;
    }
    const bool surfelGiRunsOnCompute = surfelGiQueue->queueClass == Core::CommandQueue::Compute;
    const bool clearAvboitTargets = hasTransparentRenderers || m_avboitState.m_targetsNeedClear;
    Core::GpuTimingSubmissionTicket avboitPreTimingTicket(m_graphics.gpuTiming());
    Core::GpuTimingSubmissionTicket avboitDepthWarpTimingTicket(m_graphics.gpuTiming());
    Core::GpuTimingSubmissionTicket avboitExtinctionTimingTicket(m_graphics.gpuTiming());
    Core::GpuTimingSubmissionTicket avboitIntegrationTimingTicket(m_graphics.gpuTiming());
    Core::GpuTimingSubmissionTicket avboitAccumulationTimingTicket(m_graphics.gpuTiming());
    buildAvboitTaskGraph(
        taskGraphInput,
        deferredTargets,
        csgFrameState,
        clearAvboitTargets,
        hasTransparentRenderers,
        avboitPreTimingTicket,
        avboitDepthWarpTimingTicket,
        avboitExtinctionTimingTicket,
        avboitIntegrationTimingTicket,
        avboitAccumulationTimingTicket
    );
    const Core::GpuSubmissionPacketId avboitPrePacket = m_avboitCompiledGraph.packetForTask(m_avboitPreTask);
    const Core::GpuPhysicalQueueInfo* const avboitPreQueue = avboitPrePacket.valid()
        ? m_avboitCompiledGraph.queueInfo(m_avboitCompiledGraph.packet(avboitPrePacket).queue)
        : nullptr
    ;
    const bool avboitUsesAsyncCompute = m_avboitDepthWarpTask.valid();
    const Core::GpuSubmissionPacketId avboitDepthWarpPacket = m_avboitCompiledGraph.packetForTask(
        m_avboitDepthWarpTask
    );
    const Core::GpuSubmissionPacketId avboitExtinctionPacket = m_avboitCompiledGraph.packetForTask(
        m_avboitExtinctionTask
    );
    const Core::GpuSubmissionPacketId avboitIntegrationPacket = m_avboitCompiledGraph.packetForTask(
        m_avboitIntegrationTask
    );
    const Core::GpuSubmissionPacketId avboitAccumulationPacket = m_avboitCompiledGraph.packetForTask(
        m_avboitAccumulationTask
    );
    const Core::GpuPhysicalQueueInfo* const avboitDepthWarpQueue = avboitDepthWarpPacket.valid()
        ? m_avboitCompiledGraph.queueInfo(m_avboitCompiledGraph.packet(avboitDepthWarpPacket).queue)
        : nullptr
    ;
    const Core::GpuPhysicalQueueInfo* const avboitExtinctionQueue = avboitExtinctionPacket.valid()
        ? m_avboitCompiledGraph.queueInfo(m_avboitCompiledGraph.packet(avboitExtinctionPacket).queue)
        : nullptr
    ;
    const Core::GpuPhysicalQueueInfo* const avboitIntegrationQueue = avboitIntegrationPacket.valid()
        ? m_avboitCompiledGraph.queueInfo(m_avboitCompiledGraph.packet(avboitIntegrationPacket).queue)
        : nullptr
    ;
    const Core::GpuPhysicalQueueInfo* const avboitAccumulationQueue = avboitAccumulationPacket.valid()
        ? m_avboitCompiledGraph.queueInfo(m_avboitCompiledGraph.packet(avboitAccumulationPacket).queue)
        : nullptr
    ;
    if(
        !m_avboitTaskGraphValid
        || !m_avboitPreTask.valid()
        || !m_avboitPrefixCompletion.valid()
        || !avboitPreQueue
        || avboitPreQueue->queueClass != Core::CommandQueue::Graphics
        || (
            avboitUsesAsyncCompute
            && (
                !m_avboitExtinctionTask.valid()
                || !m_avboitIntegrationTask.valid()
                || !m_avboitAccumulationTask.valid()
                || !avboitDepthWarpQueue
                || !avboitExtinctionQueue
                || !avboitIntegrationQueue
                || !avboitAccumulationQueue
                || avboitDepthWarpQueue->queueClass != Core::CommandQueue::Compute
                || avboitExtinctionQueue->queueClass != Core::CommandQueue::Graphics
                || avboitIntegrationQueue->queueClass != Core::CommandQueue::Compute
                || avboitAccumulationQueue->queueClass != Core::CommandQueue::Graphics
            )
        )
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: graph-owned AVBOIT was unavailable"));
        avboitPreTimingTicket.discard();
        avboitDepthWarpTimingTicket.discard();
        avboitExtinctionTimingTicket.discard();
        avboitIntegrationTimingTicket.discard();
        avboitAccumulationTimingTicket.discard();
        surfelGiTimingTicket.discard();
        softwareCausticsTimingTicket.discard();
        hardwareCausticsTimingTicket.discard();
        shadowVisibilityTimingTicket.discard();
        graphicsPrefixTimingTicket.discard();
        return;
    }
    Core::GpuTimingSubmissionTicket deferredLightingTimingTicket(m_graphics.gpuTiming());
    buildDeferredLightingTaskGraph(taskGraphInput, deferredTargets, deferredLightingTimingTicket);
    const Core::GpuSubmissionPacketId deferredLightingPacket = m_deferredLightingCompiledGraph.packetForTask(
        m_deferredLightingTask
    );
    const Core::GpuPhysicalQueueInfo* const deferredLightingQueue = deferredLightingPacket.valid()
        ? m_deferredLightingCompiledGraph.queueInfo(m_deferredLightingCompiledGraph.packet(deferredLightingPacket).queue)
        : nullptr
    ;
    const Core::GpuExternalCompletionId deferredLightingDependentCompletion = laggedAsyncLightingSchedule
        ? m_deferredLightingHistoryCompletion
        : m_deferredLightingSurfelGiCompletion
    ;
    if(
        !m_deferredLightingTaskGraphValid
        || !m_deferredLightingAvboitCompletion.valid()
        || !deferredLightingDependentCompletion.valid()
        || !deferredLightingQueue
        || (laggedAsyncLightingSchedule && deferredLightingQueue->queueClass != Core::CommandQueue::Graphics)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: graph-owned deferred lighting was unavailable"));
        deferredLightingTimingTicket.discard();
        avboitPreTimingTicket.discard();
        avboitDepthWarpTimingTicket.discard();
        avboitExtinctionTimingTicket.discard();
        avboitIntegrationTimingTicket.discard();
        avboitAccumulationTimingTicket.discard();
        surfelGiTimingTicket.discard();
        softwareCausticsTimingTicket.discard();
        hardwareCausticsTimingTicket.discard();
        shadowVisibilityTimingTicket.discard();
        graphicsPrefixTimingTicket.discard();
        return;
    }
    const bool deferredLightingRunsOnCompute = deferredLightingQueue->queueClass == Core::CommandQueue::Compute;
    Core::GpuTimingSubmissionTicket deferredCompositeTimingTicket(m_graphics.gpuTiming());
    buildDeferredCompositeTaskGraph(taskGraphInput, deferredTargets, deferredCompositeTimingTicket);
    // Publish the frame endpoint only after the final Graphics packet accepts.  This also covers the serialized
    // Graphics-only route when no dedicated compute family exists.
    Core::GpuTimingFrameTransaction frameTimingTransaction(m_graphics.gpuTiming());
    Optional<Core::GpuTimingMeasure> asyncPrefixTiming;
    Optional<Core::GpuTimingMeasure> asyncFinalTiming;
    Core::GpuTimingSubmissionTicket deferredPresentTimingTicket(m_graphics.gpuTiming());
    buildDeferredPresentTaskGraph(
        taskGraphInput,
        deferredTargets,
        framebuffer,
        laggedAsyncLightingSchedule,
        shadowVisibilityRunsOnCompute,
        frameTimingTransaction,
        asyncFinalTiming,
        deferredPresentTimingTicket
    );
    const Core::GpuSubmissionPacketId deferredPresentPacket = m_deferredPresentCompiledGraph.packetForTask(
        m_deferredPresentTask
    );
    const Core::GpuPhysicalQueueInfo* const deferredPresentQueue = deferredPresentPacket.valid()
        ? m_deferredPresentCompiledGraph.queueInfo(m_deferredPresentCompiledGraph.packet(deferredPresentPacket).queue)
        : nullptr
    ;
    if(
        !m_deferredPresentTaskGraphValid
        || !m_deferredPresentTask.valid()
        || !m_deferredPresentCompositeCompletion.valid()
        || (laggedAsyncLightingSchedule && !m_deferredPresentSurfelGiCompletion.valid())
        || !deferredPresentQueue
        || deferredPresentQueue->queueClass != Core::CommandQueue::Graphics
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: graph-owned deferred present was unavailable"));
        deferredPresentTimingTicket.discard();
        deferredCompositeTimingTicket.discard();
        deferredLightingTimingTicket.discard();
        avboitPreTimingTicket.discard();
        avboitDepthWarpTimingTicket.discard();
        avboitExtinctionTimingTicket.discard();
        avboitIntegrationTimingTicket.discard();
        avboitAccumulationTimingTicket.discard();
        surfelGiTimingTicket.discard();
        softwareCausticsTimingTicket.discard();
        hardwareCausticsTimingTicket.discard();
        shadowVisibilityTimingTicket.discard();
        graphicsPrefixTimingTicket.discard();
        return;
    }
    const auto discardTimingTickets = [
        &graphicsPrefixTimingTicket,
        &shadowVisibilityTimingTicket,
        &hardwareCausticsTimingTicket,
        &softwareCausticsTimingTicket,
        &surfelGiTimingTicket,
        &avboitPreTimingTicket,
        &avboitDepthWarpTimingTicket,
        &avboitExtinctionTimingTicket,
        &avboitIntegrationTimingTicket,
        &avboitAccumulationTimingTicket,
        &deferredLightingTimingTicket,
        &deferredCompositeTimingTicket,
        &deferredPresentTimingTicket
    ](){
        graphicsPrefixTimingTicket.discard();
        shadowVisibilityTimingTicket.discard();
        hardwareCausticsTimingTicket.discard();
        softwareCausticsTimingTicket.discard();
        surfelGiTimingTicket.discard();
        avboitPreTimingTicket.discard();
        avboitDepthWarpTimingTicket.discard();
        avboitExtinctionTimingTicket.discard();
        avboitIntegrationTimingTicket.discard();
        avboitAccumulationTimingTicket.discard();
        deferredLightingTimingTicket.discard();
        deferredCompositeTimingTicket.discard();
        deferredPresentTimingTicket.discard();
    };
    const auto discardUnacceptedGraphPackets = [&](){
        m_graphicsPrefixSubmissionTransaction.discardUnaccepted(
            m_graphicsPrefixTaskGraph,
            m_graphicsPrefixCompiledGraph
        );
        m_deferredPresentSubmissionTransaction.discardUnaccepted(
            m_deferredPresentTaskGraph,
            m_deferredPresentCompiledGraph
        );
        m_deferredCompositeSubmissionTransaction.discardUnaccepted(
            m_deferredCompositeTaskGraph,
            m_deferredCompositeCompiledGraph
        );
        m_deferredLightingSubmissionTransaction.discardUnaccepted(
            m_deferredLightingTaskGraph,
            m_deferredLightingCompiledGraph
        );
        m_avboitSubmissionTransaction.discardUnaccepted(
            m_avboitTaskGraph,
            m_avboitCompiledGraph
        );
        m_shadowVisibilitySubmissionTransaction.discardUnaccepted(
            m_shadowVisibilityTaskGraph,
            m_shadowVisibilityCompiledGraph
        );
        m_hardwareCausticsSubmissionTransaction.discardUnaccepted(
            m_hardwareCausticsTaskGraph,
            m_hardwareCausticsCompiledGraph
        );
        m_softwareCausticsSubmissionTransaction.discardUnaccepted(
            m_softwareCausticsTaskGraph,
            m_softwareCausticsCompiledGraph
        );
        m_surfelGiSubmissionTransaction.discardUnaccepted(
            m_surfelGiTaskGraph,
            m_surfelGiCompiledGraph
        );
    };
    const auto discardRenderPackets = [&](){
        if(asyncPrefixTiming){
            asyncPrefixTiming->discardTiming();
            asyncPrefixTiming.reset();
        }
        if(asyncFinalTiming){
            asyncFinalTiming->discardTiming();
            asyncFinalTiming.reset();
        }
        frameTimingTransaction.discard();
        discardTimingTickets();
        discardUnacceptedGraphPackets();
        restorePostGbufferPacketCpuState();
        m_raytracingSystem.discardSoftShadowTemporalHistory();
        resetAbandonedFrameStateHandoffs();
    };

    // Record independent uploads/clears in parallel; CSG clear waits for its gathered rect.
    const f32 meshViewAspectRatio = ECSRenderDetail::ResolveFramebufferAspectRatio(deferredTargets.framebuffer->getFramebufferInfo());
    bool meshViewSetupReady = false;
    bool sceneShadingSetupReady = false;
    bool meshViewSetupCommandListReady = false;
    bool sceneShadingSetupCommandListReady = false;
    bool deferredClearCommandListReady = false;
    const Core::Graphics::JobHandle meshViewSetupRecordingJob = m_graphics.scheduleGraphicsJob([
        this,
        &device,
        meshViewSetupCommandList,
        &frameTimingTransaction,
        &asyncPrefixTiming,
        &meshViewSetupReady,
        &meshViewSetupCommandListReady,
        &graphicsPrefixTimingTicket,
        meshViewAspectRatio,
        shadowVisibilityRunsOnCompute
    ](){
        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(graphicsPrefixTimingTicket);
        meshViewSetupCommandList->open(&m_shadowPrepareStateHandoff);
        if(!meshViewSetupCommandList->hasCommandBuffer())
            return;

        // The serialized Graphics route still needs the frame label in GPU-debug and crash traces.  Its timestamp
        // endpoint now spans later packets, but markers must remain balanced in this command list.
        const bool recordsGraphicsFrameMarker = !shadowVisibilityRunsOnCompute && RendererGpuTimingScope::s_Frame.valid();
        if(recordsGraphicsFrameMarker)
            meshViewSetupCommandList->beginMarker(RendererGpuTimingScope::s_Frame.markerLabel);
        const bool frameTimingStarted = frameTimingTransaction.begin(
            RendererGpuTimingScope::s_Frame,
            device,
            *meshViewSetupCommandList
        );
        if(shadowVisibilityRunsOnCompute){
            asyncPrefixTiming.emplace(
                m_graphics.gpuTiming(),
                RendererGpuTimingScope::s_AsyncPrefix,
                device,
                *meshViewSetupCommandList
            );
            asyncPrefixTiming->finishMarker();
        }

        const bool meshViewReady = m_meshSystem.updateMeshViewBuffer(*meshViewSetupCommandList, meshViewAspectRatio);
        if(recordsGraphicsFrameMarker)
            meshViewSetupCommandList->endMarker();
        meshViewSetupCommandList->close(&m_meshViewSetupStateHandoff);
        meshViewSetupReady = meshViewReady;
        meshViewSetupCommandListReady =
            frameTimingStarted
            && m_meshViewSetupStateHandoff.valid()
            && meshViewSetupCommandList->hasCommandBuffer()
        ;
    });
    const Core::Graphics::JobHandle sceneShadingSetupRecordingJob = m_graphics.scheduleGraphicsJob([
        this,
        meshViewAspectRatio,
        sceneShadingSetupCommandList,
        &sceneShadingSetupReady,
        &sceneShadingSetupCommandListReady,
        &graphicsPrefixTimingTicket
    ](){
        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(graphicsPrefixTimingTicket);
        sceneShadingSetupCommandList->open(&m_shadowPrepareStateHandoff);
        if(!sceneShadingSetupCommandList->hasCommandBuffer())
            return;

        const bool sceneShadingReady = m_deferredSystem.updateSceneShadingBuffer(*sceneShadingSetupCommandList, meshViewAspectRatio);
        sceneShadingSetupCommandList->close(&m_sceneShadingSetupStateHandoff);
        sceneShadingSetupReady = sceneShadingReady;
        sceneShadingSetupCommandListReady =
            m_sceneShadingSetupStateHandoff.valid()
            && sceneShadingSetupCommandList->hasCommandBuffer()
        ;
    });
    const Core::Graphics::JobHandle deferredClearRecordingJob = m_graphics.scheduleGraphicsJob([
        this,
        &deferredTargets,
        deferredClearCommandList,
        &deferredClearCommandListReady,
        surfelGiRunsOnCompute,
        &graphicsPrefixTimingTicket
    ](){
        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(graphicsPrefixTimingTicket);
        deferredClearCommandList->open(&m_shadowPrepareStateHandoff);
        if(!deferredClearCommandList->hasCommandBuffer())
            return;

        // CSG clear waits for its gathered receiver rect.
        m_deferredSystem.clearDeferredTargets(
            *deferredClearCommandList,
            deferredTargets,
            false,
            Core::Rect{},
            !surfelGiRunsOnCompute
        );
        deferredClearCommandList->close(&m_deferredClearStateHandoff);
        deferredClearCommandListReady =
            m_deferredClearStateHandoff.valid()
            && deferredClearCommandList->hasCommandBuffer()
        ;
    });
    if(
        !meshViewSetupRecordingJob.valid()
        || !sceneShadingSetupRecordingJob.valid()
        || !deferredClearRecordingJob.valid()
    ){
        if(meshViewSetupRecordingJob.valid())
            m_graphics.waitJob(meshViewSetupRecordingJob);
        if(sceneShadingSetupRecordingJob.valid())
            m_graphics.waitJob(sceneShadingSetupRecordingJob);
        if(deferredClearRecordingJob.valid())
            m_graphics.waitJob(deferredClearRecordingJob);
        discardRenderPackets();
        return;
    }

    m_graphics.waitJob(meshViewSetupRecordingJob);
    m_graphics.waitJob(sceneShadingSetupRecordingJob);
    m_graphics.waitJob(deferredClearRecordingJob);
    if(
        !meshViewSetupCommandListReady
        || !sceneShadingSetupCommandListReady
        || !deferredClearCommandListReady
    ){
        discardRenderPackets();
        return;
    }

    const Core::CommandListResourceStateHandoff* frameSetupBranchStates[] = {
        &m_meshViewSetupStateHandoff,
        &m_sceneShadingSetupStateHandoff,
        &m_deferredClearStateHandoff,
    };
    if(!m_frameSetupStateFanInHandoff.buildFanIn(
        m_shadowPrepareStateHandoff,
        frameSetupBranchStates,
        3u
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: frame-setup packet state fan-in failed"));
        discardRenderPackets();
        return;
    }

    const bool frameSetupReady = meshViewSetupReady && sceneShadingSetupReady;
    bool gbufferCommandListReady = false;
    const Core::Graphics::JobHandle gbufferRecordingJob = m_graphics.scheduleGraphicsJob([
        this,
        &deferredTargets,
        csgFrameState,
        hasOpaqueCsgFrameWork,
        frameSetupReady,
        &device,
        gbufferCommandList,
        &gbufferCommandListReady,
        &graphicsPrefixTimingTicket
    ](){
        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(graphicsPrefixTimingTicket);
        Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);
        Core::CommandList* commandList = gbufferCommandList;
        commandList->open(&m_frameSetupStateFanInHandoff);
        if(!commandList->hasCommandBuffer())
            return;

        MaterialPassDrawItemPartitions opaqueDrawItems{scratchArena};
        InstanceGpuDataVector instanceData{scratchArena};
        CsgFrameGpuData csgFrameData{scratchArena};
#if defined(NWB_DEBUG)
        ECSRenderDetail::MaterialTypedInstanceRangeVector materialTypedRanges{scratchArena};
#endif
        MaterialTypedByteDataVector materialTypedBytes{scratchArena};

        Core::ViewportState deferredViewportState;
        deferredViewportState.addViewportAndScissorRect(deferredTargets.framebuffer->getFramebufferInfo().getViewport());

        if(frameSetupReady){
            m_materialSystem.gatherMaterialPassDrawItems(
                deferredTargets.framebuffer.get(),
                MaterialPipelinePass::Opaque,
                false,
                csgFrameState,
                opaqueDrawItems,
                instanceData,
                csgFrameData,
#if defined(NWB_DEBUG)
                materialTypedRanges,
#endif
                materialTypedBytes,
                RendererResourceLookupMode::PreparedOnly
            );
        }

        const Core::Rect opaqueCsgClearRect = csgFrameData.workRegion.resolveRect(deferredTargets.width, deferredTargets.height);
        if(hasOpaqueCsgFrameWork)
            m_deferredSystem.clearCsgIntervalTargets(*commandList, deferredTargets, opaqueCsgClearRect);

        const bool hasDeferredDrawItems = !opaqueDrawItems.empty();
        const bool deferredResourcesReady =
            hasDeferredDrawItems
            && m_materialSystem.materialPassDrawBuffersReady(instanceData, materialTypedBytes)
        ;
        const bool regularDrawResourcesReady =
            deferredResourcesReady
            && m_materialSystem.materialPassDrawResourcesReady(opaqueDrawItems.regular)
        ;
        const bool csgResourcesReady =
            deferredResourcesReady
            && (opaqueDrawItems.csg.empty() || m_csgSystem.csgFrameBuffersReady(csgFrameData))
        ;
        const bool csgDrawResourcesReady =
            csgResourcesReady
            && (opaqueDrawItems.csg.empty() || m_materialSystem.materialPassDrawResourcesReady(opaqueDrawItems.csg))
        ;
        const bool csgReceiverSurfaceDrawResourcesReady =
            csgResourcesReady
            && (opaqueDrawItems.csgReceiverSurface.empty() || m_materialSystem.materialPassDrawResourcesReady(opaqueDrawItems.csgReceiverSurface))
        ;
        const bool deferredUploadReady =
            deferredResourcesReady
            && m_materialSystem.uploadMaterialPassDrawBuffers(
                *commandList,
                instanceData,
#if defined(NWB_DEBUG)
                materialTypedRanges,
#endif
                materialTypedBytes
            )
        ;
        if(deferredUploadReady){
            const bool csgUploadReady = csgResourcesReady && (opaqueDrawItems.csg.empty() || m_csgSystem.uploadCsgFrameBuffers(*commandList, csgFrameData));
            const bool csgSampleStateReady =
                csgUploadReady
                && (!csgFrameData.hasWork() || m_csgSystem.uploadCsgIntervalSampleState(*commandList, deferredTargets, csgFrameData))
            ;
            if(csgSampleStateReady && csgFrameData.hasWork())
                m_csgSystem.dispatchCsgIntervalPeels(*commandList, deferredTargets, csgFrameData);
            const MaterialPassDrawContext opaqueDrawContext{
                *commandList,
                deferredTargets.framebuffer.get(),
                MaterialPipelinePass::Opaque,
                nullptr,
                deferredViewportState
            };
            if(regularDrawResourcesReady && !opaqueDrawItems.regular.empty()){
                Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_OpaqueRegular, device, *commandList);

                m_materialSystem.renderMaterialPassDrawItems(opaqueDrawContext, opaqueDrawItems.regular);
            }

            Core::ViewportState csgIntervalViewportState;
            csgIntervalViewportState
                .addViewport(deferredTargets.framebuffer->getFramebufferInfo().getViewport())
                .addScissorRect(csgFrameData.workRegion.resolveRect(deferredTargets.width, deferredTargets.height))
            ;
            const MaterialPassDrawContext csgReceiverSurfaceDrawContext{
                *commandList,
                deferredTargets.framebuffer.get(),
                MaterialPipelinePass::CsgReceiverSurface,
                nullptr,
                csgIntervalViewportState
            };
            if(csgSampleStateReady && csgReceiverSurfaceDrawResourcesReady && !opaqueDrawItems.csgReceiverSurface.empty()){
                Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_OpaqueCsgReceiverSurface, device, *commandList);

                m_materialSystem.renderMaterialPassDrawItems(csgReceiverSurfaceDrawContext, opaqueDrawItems.csgReceiverSurface);
            }
            if(csgSampleStateReady && csgFrameData.hasWork() && csgReceiverSurfaceDrawResourcesReady)
                m_csgSystem.dispatchCsgReceiverSpanBuild(*commandList, deferredTargets, csgFrameData);
            if(csgSampleStateReady && csgFrameData.hasWork() && csgReceiverSurfaceDrawResourcesReady)
                m_csgSystem.dispatchCsgIntervalCombine(*commandList, deferredTargets, csgFrameData);
            if(csgSampleStateReady && csgDrawResourcesReady){
                if(!opaqueDrawItems.csg.empty()){
                    Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_OpaqueCsg, device, *commandList);

                    m_materialSystem.renderMaterialPassDrawItems(opaqueDrawContext, opaqueDrawItems.csg);
                }
                if(csgFrameData.hasWork() && csgReceiverSurfaceDrawResourcesReady)
                    m_csgSystem.renderCsgIntervalCaps(*commandList, deferredTargets, csgFrameData);
            }
        }
        commandList->endRenderPass();

        // Sibling workers import the opaque producer's exported state.
        commandList->close(&m_gbufferStateHandoff);
        if(!m_gbufferStateHandoff.valid())
            return;
        gbufferCommandListReady = commandList->hasCommandBuffer();
    });
    if(!gbufferRecordingJob.valid()){
        discardRenderPackets();
        return;
    }

    m_graphics.waitJob(gbufferRecordingJob);
    if(!gbufferCommandListReady){
        discardRenderPackets();
        return;
    }

    // Normalize shared inputs before sibling workers import the same snapshot.
    bool postGbufferNormalizeCommandListReady = false;
    const Core::Graphics::JobHandle postGbufferNormalizeRecordingJob = m_graphics.scheduleGraphicsJob([
        this,
        &deferredTargets,
        postGbufferNormalizeCommandList,
        &postGbufferNormalizeCommandListReady,
        &asyncPrefixTiming,
        shadowVisibilityRunsOnCompute,
        &graphicsPrefixTimingTicket
    ](){
        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(graphicsPrefixTimingTicket);
        postGbufferNormalizeCommandList->open(&m_gbufferStateHandoff);
        if(!postGbufferNormalizeCommandList->hasCommandBuffer())
            return;

        m_raytracingSystem.normalizePostGbufferPacketResources(*postGbufferNormalizeCommandList, deferredTargets);
        if(shadowVisibilityRunsOnCompute && asyncPrefixTiming){
            asyncPrefixTiming->finishTiming(*postGbufferNormalizeCommandList);
            asyncPrefixTiming.reset();
        }
        postGbufferNormalizeCommandList->close(&m_postGbufferNormalizedStateHandoff);
        postGbufferNormalizeCommandListReady =
            m_postGbufferNormalizedStateHandoff.valid()
            && postGbufferNormalizeCommandList->hasCommandBuffer()
        ;
    });
    if(!postGbufferNormalizeRecordingJob.valid()){
        discardRenderPackets();
        return;
    }

    m_graphics.waitJob(postGbufferNormalizeRecordingJob);
    if(!postGbufferNormalizeCommandListReady){
        discardRenderPackets();
        return;
    }

    // Compute imports only shared shadow inputs plus retained compute-local state.
    if(shadowVisibilityRunsOnCompute){
        Core::Alloc::ScratchArena shadowInputScratchArena(RendererArenaScope::s_RenderArena);
        Vector<Core::Texture*, Core::Alloc::ScratchArena> shadowInputTextures{ shadowInputScratchArena };
        Vector<Core::Buffer*, Core::Alloc::ScratchArena> shadowInputBuffers{ shadowInputScratchArena };
        const auto appendTexture = [&](Core::Texture* texture){
            if(texture)
                shadowInputTextures.push_back(texture);
        };
        const auto appendBuffer = [&](Core::Buffer* buffer){
            if(buffer)
                shadowInputBuffers.push_back(buffer);
        };

        appendTexture(deferredTargets.worldPosition.get());
        appendTexture(deferredTargets.normal.get());
        appendTexture(deferredTargets.depth.get());
        appendBuffer(m_deferredState.m_sceneShadingBuffer.get());
        appendBuffer(m_deferredState.m_lightBuffer.get());
        appendBuffer(deferredTargets.bindless.slotsBuffer.get());
        appendBuffer(m_rayTracingState.m_sceneBvhNodeBuffer.get());
        appendBuffer(m_rayTracingState.m_sceneInstanceBuffer.get());
        appendBuffer(m_rayTracingState.m_shadowInstanceMaterialBuffer.get());
        appendBuffer(m_rayTracingState.m_shadowInstanceBuffer.get());
        appendBuffer(m_rayTracingState.m_shadowMaterialTypedBuffer.get());
        appendBuffer(m_rayTracingState.m_rayTraceMaterialContextSlotsBuffer.get());
        if(m_rayTracingState.m_tlas)
            appendBuffer(m_rayTracingState.m_tlas->getBackingBuffer());
        for(Core::Buffer* buffer : m_rayTracingState.m_swShadowMeshNodeBuffers)
            appendBuffer(buffer);
        for(Core::Buffer* buffer : m_rayTracingState.m_swShadowMeshPositionBuffers)
            appendBuffer(buffer);
        for(Core::Buffer* buffer : m_rayTracingState.m_swShadowMeshIndexBuffers)
            appendBuffer(buffer);
        for(Core::Buffer* buffer : m_rayTracingState.m_swShadowMeshAttributeBuffers)
            appendBuffer(buffer);

        if(!m_shadowComputeBaseStateHandoff.buildResourceSubset(
            m_postGbufferNormalizedStateHandoff,
            shadowInputTextures.data(),
            shadowInputTextures.size(),
            shadowInputBuffers.data(),
            shadowInputBuffers.size()
        )){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: async shadow input state selection failed"));
            discardRenderPackets();
            return;
        }

        const Core::CommandListResourceStateHandoff* shadowInputBranches[2] = {};
        usize shadowInputBranchCount = 0u;
        if(m_shadowComputePersistentStateHandoff.valid())
            shadowInputBranches[shadowInputBranchCount++] = &m_shadowComputePersistentStateHandoff;
        if(m_shadowVisibilityReturnStateHandoff.valid())
            shadowInputBranches[shadowInputBranchCount++] = &m_shadowVisibilityReturnStateHandoff;
        if(!m_shadowComputeInputStateHandoff.buildFanIn(
            m_shadowComputeBaseStateHandoff,
            shadowInputBranches,
            shadowInputBranchCount
        )){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: async shadow input state fan-in failed"));
            discardRenderPackets();
            return;
        }
    }

    // The graph-selected Compute route imports software-caustics inputs plus retained private scratch. Graphics
    // fallback records from the normalized G-buffer handoff directly.
    if(softwareCausticsRunsOnCompute){
        Core::Alloc::ScratchArena causticsInputScratchArena(RendererArenaScope::s_RenderArena);
        Vector<Core::Texture*, Core::Alloc::ScratchArena> causticsInputTextures{ causticsInputScratchArena };
        Vector<Core::Buffer*, Core::Alloc::ScratchArena> causticsInputBuffers{ causticsInputScratchArena };
        const auto appendTexture = [&](Core::Texture* texture){
            if(texture)
                causticsInputTextures.push_back(texture);
        };
        const auto appendBuffer = [&](Core::Buffer* buffer){
            if(buffer)
                causticsInputBuffers.push_back(buffer);
        };

        appendTexture(deferredTargets.worldPosition.get());
        appendTexture(deferredTargets.depth.get());
        appendBuffer(m_deferredState.m_sceneShadingBuffer.get());
        appendBuffer(m_deferredState.m_lightBuffer.get());
        appendBuffer(deferredTargets.bindless.slotsBuffer.get());
        appendBuffer(m_rayTracingState.m_sceneBvhNodeBuffer.get());
        appendBuffer(m_rayTracingState.m_sceneInstanceBuffer.get());
        appendBuffer(m_rayTracingState.m_shadowInstanceMaterialBuffer.get());
        appendBuffer(m_rayTracingState.m_shadowInstanceBuffer.get());
        appendBuffer(m_rayTracingState.m_shadowMaterialTypedBuffer.get());
        appendBuffer(m_rayTracingState.m_rayTraceMaterialContextSlotsBuffer.get());
        appendBuffer(m_rayTracingState.m_causticEmissionTargetBuffer.get());
        appendBuffer(m_drawState.m_meshViewBuffer.get());
        for(Core::Buffer* buffer : m_rayTracingState.m_swShadowMeshNodeBuffers)
            appendBuffer(buffer);
        for(Core::Buffer* buffer : m_rayTracingState.m_swShadowMeshPositionBuffers)
            appendBuffer(buffer);
        for(Core::Buffer* buffer : m_rayTracingState.m_swShadowMeshIndexBuffers)
            appendBuffer(buffer);
        for(Core::Buffer* buffer : m_rayTracingState.m_swShadowMeshAttributeBuffers)
            appendBuffer(buffer);

        if(!m_causticsComputeBaseStateHandoff.buildResourceSubset(
            m_postGbufferNormalizedStateHandoff,
            causticsInputTextures.data(),
            causticsInputTextures.size(),
            causticsInputBuffers.data(),
            causticsInputBuffers.size()
        )){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: async caustics input state selection failed"));
            discardRenderPackets();
            return;
        }

        const Core::CommandListResourceStateHandoff* causticsInputBranches[2] = {};
        usize causticsInputBranchCount = 0u;
        if(m_causticsComputePersistentStateHandoff.valid())
            causticsInputBranches[causticsInputBranchCount++] = &m_causticsComputePersistentStateHandoff;
        if(m_causticIrradianceReturnStateHandoff.valid())
            causticsInputBranches[causticsInputBranchCount++] = &m_causticIrradianceReturnStateHandoff;
        if(!m_causticsComputeInputStateHandoff.buildFanIn(
            m_causticsComputeBaseStateHandoff,
            causticsInputBranches,
            causticsInputBranchCount
        )){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: async caustics input state fan-in failed"));
            discardRenderPackets();
            return;
        }
    }

    // The graph-selected Compute route imports current G-buffer inputs and retained field state. Graphics uses the
    // established prefix handoff directly.
    if(surfelGiRunsOnCompute){
        Core::Alloc::ScratchArena surfelGiInputScratchArena(RendererArenaScope::s_RenderArena);
        Vector<Core::Texture*, Core::Alloc::ScratchArena> surfelGiInputTextures{ surfelGiInputScratchArena };
        Vector<Core::Buffer*, Core::Alloc::ScratchArena> surfelGiInputBuffers{ surfelGiInputScratchArena };
        const auto appendTexture = [&](Core::Texture* texture){
            if(texture)
                surfelGiInputTextures.push_back(texture);
        };
        const auto appendBuffer = [&](Core::Buffer* buffer){
            if(buffer)
                surfelGiInputBuffers.push_back(buffer);
        };

        appendTexture(deferredTargets.worldPosition.get());
        appendTexture(deferredTargets.normal.get());
        appendBuffer(m_deferredState.m_sceneShadingBuffer.get());
        appendBuffer(m_deferredState.m_lightBuffer.get());
        appendBuffer(deferredTargets.bindless.slotsBuffer.get());
        appendBuffer(m_rayTracingState.m_surfelConstants.get());
        appendBuffer(m_rayTracingState.m_sceneBvhNodeBuffer.get());
        appendBuffer(m_rayTracingState.m_sceneInstanceBuffer.get());
        appendBuffer(m_rayTracingState.m_shadowInstanceMaterialBuffer.get());
        appendBuffer(m_rayTracingState.m_shadowInstanceBuffer.get());
        appendBuffer(m_rayTracingState.m_shadowMaterialTypedBuffer.get());
        appendBuffer(m_rayTracingState.m_rayTraceMaterialContextSlotsBuffer.get());
        if(m_rayTracingState.m_tlas)
            appendBuffer(m_rayTracingState.m_tlas->getBackingBuffer());
        for(Core::Buffer* buffer : m_rayTracingState.m_swShadowMeshNodeBuffers)
            appendBuffer(buffer);
        for(Core::Buffer* buffer : m_rayTracingState.m_swShadowMeshPositionBuffers)
            appendBuffer(buffer);
        for(Core::Buffer* buffer : m_rayTracingState.m_swShadowMeshIndexBuffers)
            appendBuffer(buffer);
        for(Core::Buffer* buffer : m_rayTracingState.m_swShadowMeshAttributeBuffers)
            appendBuffer(buffer);

        if(!m_surfelGiComputeBaseStateHandoff.buildResourceSubset(
            m_postGbufferNormalizedStateHandoff,
            surfelGiInputTextures.data(),
            surfelGiInputTextures.size(),
            surfelGiInputBuffers.data(),
            surfelGiInputBuffers.size()
        )){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: async surfel-GI input state selection failed"));
            discardRenderPackets();
            return;
        }

        const Core::CommandListResourceStateHandoff* surfelGiInputBranches[2] = {};
        usize surfelGiInputBranchCount = 0u;
        if(m_surfelGiComputePersistentStateHandoff.valid())
            surfelGiInputBranches[surfelGiInputBranchCount++] = &m_surfelGiComputePersistentStateHandoff;
        if(m_surfelIrradianceReturnStateHandoff.valid())
            surfelGiInputBranches[surfelGiInputBranchCount++] = &m_surfelIrradianceReturnStateHandoff;
        if(!m_surfelGiComputeInputStateHandoff.buildFanIn(
            m_surfelGiComputeBaseStateHandoff,
            surfelGiInputBranches,
            surfelGiInputBranchCount
        )){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: async surfel-GI input state fan-in failed"));
            discardRenderPackets();
            return;
        }
    }

    // Shadow visibility owns its native graph packet. The manual state bridge remains until the barrier phase.
    Core::GpuNativePacketRecorder shadowVisibilityRecorder(device);
    const Core::GpuNativePacketRecordDesc shadowVisibilityRecordDesc{
        .packet = shadowVisibilityPacket,
        .initialStates = shadowVisibilityRunsOnCompute
            ? &m_shadowComputeInputStateHandoff
            : &m_postGbufferNormalizedStateHandoff,
        .finalStates = &m_shadowVisibilityStateHandoff,
    };
    const bool shadowVisibilityRecorded =
        m_shadowVisibilityTaskGraphValid
        && m_shadowVisibilityTask.valid()
        && m_shadowVisibilityPrefixCompletion.valid()
        && shadowVisibilityPacket.valid()
        && shadowVisibilityRecorder.recordPacket(
            m_shadowVisibilityTaskGraph,
            m_shadowVisibilityCompiledGraph,
            shadowVisibilityRecordDesc,
            m_shadowVisibilityRecordedGraph
        )
        && m_shadowVisibilityStateHandoff.valid()
    ;
    if(!shadowVisibilityRecorded){
        shadowVisibilityTimingTicket.discard();
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to record graph-owned shadow visibility"));
        discardRenderPackets();
        return;
    }

    // Hardware dispatch-rays caustics own a Graphics graph packet. Their imported lagged-history completion, when
    // active, is resolved at submission; the manual state handoff remains until the barrier phase.
    if(hardwareShadowSupported){
        Core::GpuNativePacketRecorder hardwareCausticsRecorder(device);
        const Core::GpuNativePacketRecordDesc hardwareCausticsRecordDesc{
            .packet = hardwareCausticsPacket,
            .initialStates = &m_postGbufferNormalizedStateHandoff,
            .finalStates = &m_causticsStateHandoff,
        };
        const bool hardwareCausticsRecorded =
            m_hardwareCausticsTaskGraphValid
            && m_hardwareCausticsTask.valid()
            && m_hardwareCausticsPrefixCompletion.valid()
            && (!laggedAsyncLightingSchedule || m_hardwareCausticsLaggedHistoryCompletion.valid())
            && hardwareCausticsPacket.valid()
            && hardwareCausticsRecorder.recordPacket(
                m_hardwareCausticsTaskGraph,
                m_hardwareCausticsCompiledGraph,
                hardwareCausticsRecordDesc,
                m_hardwareCausticsRecordedGraph
            )
            && m_causticsStateHandoff.valid()
        ;
        if(!hardwareCausticsRecorded){
            m_hardwareCausticsSubmissionTransaction.discardUnaccepted(
                m_hardwareCausticsTaskGraph,
                m_hardwareCausticsCompiledGraph
            );
            hardwareCausticsTimingTicket.discard();
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to record graph-owned hardware caustics"));
            discardRenderPackets();
            return;
        }
    }

    // Software caustics own their native packet rather than an async persistent list. The shadow-visibility
    // completion retains their historical order, while the manual state bridge remains until the barrier phase.
    if(!hardwareShadowSupported){
        Core::GpuNativePacketRecorder softwareCausticsRecorder(device);
        const Core::GpuNativePacketRecordDesc softwareCausticsRecordDesc{
            .packet = softwareCausticsPacket,
            .initialStates = softwareCausticsRunsOnCompute
                ? &m_causticsComputeInputStateHandoff
                : &m_postGbufferNormalizedStateHandoff,
            .finalStates = &m_causticsStateHandoff,
        };
        const bool softwareCausticsRecorded =
            m_softwareCausticsTaskGraphValid
            && m_softwareCausticsTask.valid()
            && m_softwareCausticsShadowVisibilityCompletion.valid()
            && softwareCausticsPacket.valid()
            && softwareCausticsRecorder.recordPacket(
                m_softwareCausticsTaskGraph,
                m_softwareCausticsCompiledGraph,
                softwareCausticsRecordDesc,
                m_softwareCausticsRecordedGraph
            )
            && m_causticsStateHandoff.valid()
        ;
        if(!softwareCausticsRecorded){
            m_softwareCausticsSubmissionTransaction.discardUnaccepted(
                m_softwareCausticsTaskGraph,
                m_softwareCausticsCompiledGraph
            );
            softwareCausticsTimingTicket.discard();
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to record graph-owned software caustics"));
            discardRenderPackets();
            return;
        }
    }

    // Surfel GI records only after the graph resolved its physical queue. Its imported completion follows software
    // caustics or graph-owned shadow visibility; state fan-in stays manual until Phase 6.
    Core::GpuNativePacketRecorder surfelGiRecorder(device);
    const Core::GpuNativePacketRecordDesc surfelGiRecordDesc{
        .packet = surfelGiPacket,
        .initialStates = surfelGiRunsOnCompute
            ? &m_surfelGiComputeInputStateHandoff
            : &m_postGbufferNormalizedStateHandoff,
        .finalStates = &m_surfelGiStateHandoff,
    };
    const bool surfelGiRecorded = surfelGiPacket.valid()
        && surfelGiRecorder.recordPacket(
            m_surfelGiTaskGraph,
            m_surfelGiCompiledGraph,
            surfelGiRecordDesc,
            m_surfelGiRecordedGraph
        )
        && m_surfelGiStateHandoff.valid()
    ;
    if(!surfelGiRecorded){
        m_surfelGiSubmissionTransaction.discardUnaccepted(
            m_surfelGiTaskGraph,
            m_surfelGiCompiledGraph
        );
        surfelGiTimingTicket.discard();
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to record graph-owned surfel GI"));
        discardRenderPackets();
        return;
    }

    // AVBOIT now records graph-selected packets. The clear mirror changes with this tentative recording and is
    // restored by discardRenderPackets until the pre packet accepts.
    m_avboitState.m_targetsNeedClear = hasTransparentRenderers;
    Core::GpuNativePacketRecorder avboitRecorder(device);
    const Core::GpuNativePacketRecordDesc avboitPreRecordDesc{
        .packet = avboitPrePacket,
        .initialStates = &m_postGbufferNormalizedStateHandoff,
    };
    const bool avboitPreRecorded =
        m_avboitTaskGraphValid
        && m_avboitPreTask.valid()
        && m_avboitPrefixCompletion.valid()
        && avboitPrePacket.valid()
        && avboitRecorder.recordPacket(
            m_avboitTaskGraph,
            m_avboitCompiledGraph,
            avboitPreRecordDesc,
            m_avboitRecordedGraph
        )
    ;
    if(!avboitPreRecorded){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to record graph-owned AVBOIT pre packet"));
        discardRenderPackets();
        return;
    }

    if(avboitUsesAsyncCompute){
        const Core::GpuNativePacketRecordDesc avboitDepthWarpRecordDesc{
            .packet = avboitDepthWarpPacket,
            .useCompiledStateSeeds = true,
            .applyCompiledBarriers = true,
        };
        const bool avboitDepthWarpRecorded =
            avboitDepthWarpPacket.valid()
            && avboitRecorder.recordPacket(
                m_avboitTaskGraph,
                m_avboitCompiledGraph,
                avboitDepthWarpRecordDesc,
                m_avboitRecordedGraph
            )
        ;
        if(!avboitDepthWarpRecorded){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to record graph-owned AVBOIT depth warp"));
            discardRenderPackets();
            return;
        }

        const Core::GpuNativePacketRecordDesc avboitExtinctionRecordDesc{
            .packet = avboitExtinctionPacket,
            .useCompiledStateSeeds = true,
            .applyCompiledBarriers = true,
        };
        const bool avboitExtinctionRecorded =
            avboitExtinctionPacket.valid()
            && avboitRecorder.recordPacket(
                m_avboitTaskGraph,
                m_avboitCompiledGraph,
                avboitExtinctionRecordDesc,
                m_avboitRecordedGraph
            )
        ;
        if(!avboitExtinctionRecorded){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to record graph-owned AVBOIT extinction"));
            discardRenderPackets();
            return;
        }

        const Core::GpuNativePacketRecordDesc avboitIntegrationRecordDesc{
            .packet = avboitIntegrationPacket,
            .useCompiledStateSeeds = true,
            .applyCompiledBarriers = true,
        };
        const bool avboitIntegrationRecorded =
            avboitIntegrationPacket.valid()
            && avboitRecorder.recordPacket(
                m_avboitTaskGraph,
                m_avboitCompiledGraph,
                avboitIntegrationRecordDesc,
                m_avboitRecordedGraph
            )
        ;
        if(!avboitIntegrationRecorded){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to record graph-owned AVBOIT integration"));
            discardRenderPackets();
            return;
        }

        const Core::GpuNativePacketRecordDesc avboitAccumulationRecordDesc{
            .packet = avboitAccumulationPacket,
            .useCompiledStateSeeds = true,
            .applyCompiledBarriers = true,
        };
        const bool avboitAccumulationRecorded =
            avboitAccumulationPacket.valid()
            && avboitRecorder.recordPacket(
                m_avboitTaskGraph,
                m_avboitCompiledGraph,
                avboitAccumulationRecordDesc,
                m_avboitRecordedGraph
            )
        ;
        if(!avboitAccumulationRecorded){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to record graph-owned AVBOIT accumulation"));
            discardRenderPackets();
            return;
        }
    }

    const Core::GpuSubmissionPacketId avboitFinalPacket = avboitUsesAsyncCompute
        ? avboitAccumulationPacket
        : avboitPrePacket
    ;
    const Core::CommandListResourceStateHandoff* const avboitFinalStateSeed =
        m_avboitRecordedGraph.packetFinalStateSeed(avboitFinalPacket)
    ;
    if(!avboitFinalStateSeed){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: AVBOIT graph did not retain its final state seed"));
        discardRenderPackets();
        return;
    }

    // Lighting imports live effects only on the default path; lagged mode uses accepted history.
    Core::Texture* const deferredLightingBaseTextures[] = {
        deferredTargets.albedo.get(),
        deferredTargets.normal.get(),
        deferredTargets.worldPosition.get(),
        deferredTargets.depth.get(),
        deferredTargets.opaqueColor.get(),
    };
    Core::Buffer* const deferredLightingBaseBuffers[] = {
        m_deferredState.m_sceneShadingBuffer.get(),
        m_deferredState.m_lightBuffer.get(),
        deferredTargets.bindless.slotsBuffer.get(),
    };
    if(!m_deferredLightingBaseStateHandoff.buildResourceSubset(
        m_postGbufferNormalizedStateHandoff,
        deferredLightingBaseTextures,
        LengthOf(deferredLightingBaseTextures),
        deferredLightingBaseBuffers,
        LengthOf(deferredLightingBaseBuffers)
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: deferred-lighting base state selection failed"));
        discardRenderPackets();
        return;
    }
    if(!m_shadowVisibilityLightingStateHandoff.buildTextureSubset(
        m_shadowVisibilityStateHandoff,
        deferredTargets.shadowVisibility.get()
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to isolate the shadow visibility lighting handoff"));
        discardRenderPackets();
        return;
    }
    if(!m_causticIrradianceLightingStateHandoff.buildTextureSubset(
        m_causticsStateHandoff,
        deferredTargets.causticIrradiance.get()
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to isolate the caustic irradiance lighting handoff"));
        discardRenderPackets();
        return;
    }
    if(!m_surfelIrradianceLightingStateHandoff.buildTextureSubset(
        m_surfelGiStateHandoff,
        deferredTargets.surfelIrradiance.get()
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to isolate the surfel irradiance lighting handoff"));
        discardRenderPackets();
        return;
    }
    Core::Texture* const avboitLightingTextures[] = {
        deferredTargets.albedo.get(),
        deferredTargets.normal.get(),
        deferredTargets.worldPosition.get(),
        deferredTargets.depth.get(),
    };
    if(!m_avboitLightingStateHandoff.buildResourceSubset(
        *avboitFinalStateSeed,
        avboitLightingTextures,
        LengthOf(avboitLightingTextures),
        nullptr,
        0u
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to isolate the AVBOIT lighting inputs"));
        discardRenderPackets();
        return;
    }

    const Core::CommandListResourceStateHandoff* deferredLightingBranchStates[4] = {};
    usize deferredLightingBranchCount = 0u;
    if(!laggedAsyncLightingSchedule){
        deferredLightingBranchStates[deferredLightingBranchCount++] = &m_shadowVisibilityLightingStateHandoff;
        deferredLightingBranchStates[deferredLightingBranchCount++] = &m_causticIrradianceLightingStateHandoff;
        deferredLightingBranchStates[deferredLightingBranchCount++] = &m_surfelIrradianceLightingStateHandoff;
    }
    deferredLightingBranchStates[deferredLightingBranchCount++] = &m_avboitLightingStateHandoff;
    if(!m_deferredLightingInputStateHandoff.buildFanIn(
        m_deferredLightingBaseStateHandoff,
        deferredLightingBranchStates,
        deferredLightingBranchCount
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: deferred-lighting state fan-in failed"));
        discardRenderPackets();
        return;
    }

    // The graph owns native recording and packet-boundary barriers.  The handoff only seeds actual cross-graph state
    // until compiled packet-state seeds eliminate renderer-owned fan-in.
    Core::GpuNativePacketRecorder deferredLightingRecorder(device);
    const Core::GpuNativePacketRecordDesc deferredLightingRecordDesc{
        .packet = deferredLightingPacket,
        .initialStates = &m_deferredLightingInputStateHandoff,
        .finalStates = &m_deferredLightingStateHandoff,
        .applyCompiledBarriers = true,
    };
    const bool deferredLightingRecorded =
        m_deferredLightingTaskGraphValid
        && m_deferredLightingTask.valid()
        && m_deferredLightingAvboitCompletion.valid()
        && deferredLightingDependentCompletion.valid()
        && deferredLightingPacket.valid()
        && deferredLightingRecorder.recordPacket(
            m_deferredLightingTaskGraph,
            m_deferredLightingCompiledGraph,
            deferredLightingRecordDesc,
            m_deferredLightingRecordedGraph
        )
        && m_deferredLightingStateHandoff.valid()
    ;
    if(!deferredLightingRecorded){
        m_deferredLightingSubmissionTransaction.discardUnaccepted(
            m_deferredLightingTaskGraph,
            m_deferredLightingCompiledGraph
        );
        deferredLightingTimingTicket.discard();
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to record graph-owned deferred lighting"));
        discardRenderPackets();
        return;
    }

    if(!m_opaqueColorCompositeStateHandoff.buildTextureSubset(
        m_deferredLightingStateHandoff,
        deferredTargets.opaqueColor.get()
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to isolate the deferred-lighting output for Compute composite"));
        discardRenderPackets();
        return;
    }
    Core::Texture* const avboitCompositeTextures[] = {
        deferredTargets.avboit.accumColor.get(),
        deferredTargets.avboit.accumExtinction.get(),
    };
    if(!m_avboitCompositeStateHandoff.buildResourceSubset(
        *avboitFinalStateSeed,
        avboitCompositeTextures,
        LengthOf(avboitCompositeTextures),
        nullptr,
        0u
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to isolate the AVBOIT composite inputs"));
        discardRenderPackets();
        return;
    }
    Core::Buffer* const deferredCompositeBaseBuffers[] = {
        deferredTargets.bindless.slotsBuffer.get(),
    };
    if(!m_deferredCompositeBaseStateHandoff.buildResourceSubset(
        m_deferredLightingBaseStateHandoff,
        nullptr,
        0u,
        deferredCompositeBaseBuffers,
        LengthOf(deferredCompositeBaseBuffers)
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: deferred-composite base state selection failed"));
        discardRenderPackets();
        return;
    }
    const Core::CommandListResourceStateHandoff* deferredCompositeBranchStates[] = {
        &m_avboitCompositeStateHandoff,
        &m_opaqueColorCompositeStateHandoff,
    };
    if(!m_deferredCompositeInputStateHandoff.buildFanIn(
        m_deferredCompositeBaseStateHandoff,
        deferredCompositeBranchStates,
        2u
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: deferred-composite state fan-in failed"));
        discardRenderPackets();
        return;
    }

    const Core::GpuSubmissionPacketId deferredCompositePacket = m_deferredCompositeCompiledGraph.packetForTask(
        m_deferredCompositeTask
    );
    Core::GpuNativePacketRecorder deferredCompositeRecorder(device);
    const Core::GpuNativePacketRecordDesc deferredCompositeRecordDesc{
        .packet = deferredCompositePacket,
        .initialStates = &m_deferredCompositeInputStateHandoff,
        .finalStates = &m_deferredCompositeStateHandoff,
        .applyCompiledBarriers = true,
    };
    const bool deferredCompositeRecorded =
        m_deferredCompositeTaskGraphValid
        && m_deferredCompositeTask.valid()
        && m_deferredCompositeLightingCompletion.valid()
        && deferredCompositePacket.valid()
        && deferredCompositeRecorder.recordPacket(
            m_deferredCompositeTaskGraph,
            m_deferredCompositeCompiledGraph,
            deferredCompositeRecordDesc,
            m_deferredCompositeRecordedGraph
        )
        && m_deferredCompositeStateHandoff.valid()
    ;
    if(!deferredCompositeRecorded){
        m_deferredCompositeSubmissionTransaction.discardUnaccepted(
            m_deferredCompositeTaskGraph,
            m_deferredCompositeCompiledGraph
        );
        deferredCompositeTimingTicket.discard();
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to record graph-owned deferred composite"));
        discardRenderPackets();
        return;
    }

    if(!m_compositeColorPresentStateHandoff.buildTextureSubset(
        m_deferredCompositeStateHandoff,
        deferredTargets.compositeColor.get()
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to isolate the Compute composite output for Graphics presentation"));
        discardRenderPackets();
        return;
    }
    Core::Buffer* const deferredPresentBaseBuffers[] = {
        deferredTargets.bindless.slotsBuffer.get(),
    };
    if(!m_deferredPresentBaseStateHandoff.buildResourceSubset(
        m_deferredCompositeBaseStateHandoff,
        nullptr,
        0u,
        deferredPresentBaseBuffers,
        LengthOf(deferredPresentBaseBuffers)
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: deferred-present base state selection failed"));
        discardRenderPackets();
        return;
    }
    const Core::CommandListResourceStateHandoff* deferredPresentBranchStates[] = {
        &m_compositeColorPresentStateHandoff,
    };
    if(!m_deferredPresentInputStateHandoff.buildFanIn(
        m_deferredPresentBaseStateHandoff,
        deferredPresentBranchStates,
        1u
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: deferred-present state fan-in failed"));
        discardRenderPackets();
        return;
    }

    Core::GpuNativePacketRecorder deferredPresentRecorder(device);
    const Core::GpuNativePacketRecordDesc deferredPresentRecordDesc{
        .packet = deferredPresentPacket,
        .initialStates = &m_deferredPresentInputStateHandoff,
        .finalStates = &m_deferredPresentStateHandoff,
        .applyCompiledBarriers = true,
    };
    const bool deferredPresentRecorded =
        m_deferredPresentTaskGraphValid
        && m_deferredPresentTask.valid()
        && m_deferredPresentCompositeCompletion.valid()
        && (!laggedAsyncLightingSchedule || m_deferredPresentSurfelGiCompletion.valid())
        && deferredPresentPacket.valid()
        && deferredPresentRecorder.recordPacket(
            m_deferredPresentTaskGraph,
            m_deferredPresentCompiledGraph,
            deferredPresentRecordDesc,
            m_deferredPresentRecordedGraph
        )
        && m_deferredPresentStateHandoff.valid()
    ;
    if(!deferredPresentRecorded){
        m_deferredPresentSubmissionTransaction.discardUnaccepted(
            m_deferredPresentTaskGraph,
            m_deferredPresentCompiledGraph
        );
        deferredPresentTimingTicket.discard();
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to record graph-owned deferred present"));
        discardRenderPackets();
        return;
    }

    // The graph packet imports the established parallel command-list bundle and becomes its sole submission owner.
    Core::CommandList* const graphicsPrefixCommandLists[] = {
        meshViewSetupCommandList,
        sceneShadingSetupCommandList,
        deferredClearCommandList,
        gbufferCommandList,
        postGbufferNormalizeCommandList,
    };
    Core::GpuNativePacketRecorder graphicsPrefixRecorder(device);
    const Core::GpuImportedPacketRecordDesc graphicsPrefixRecordDesc{
        .packet = graphicsPrefixPacket,
        .commandLists = graphicsPrefixCommandLists,
        .commandListCount = LengthOf(graphicsPrefixCommandLists),
    };
    const bool graphicsPrefixRecorded =
        m_graphicsPrefixTaskGraphValid
        && m_graphicsPrefixTask.valid()
        && graphicsPrefixPacket.valid()
        && graphicsPrefixRecorder.recordImportedPacket(
            m_graphicsPrefixTaskGraph,
            m_graphicsPrefixCompiledGraph,
            graphicsPrefixRecordDesc,
            m_graphicsPrefixRecordedGraph
        )
    ;
    if(!graphicsPrefixRecorded){
        m_graphicsPrefixSubmissionTransaction.discardUnaccepted(
            m_graphicsPrefixTaskGraph,
            m_graphicsPrefixCompiledGraph
        );
        graphicsPrefixTimingTicket.discard();
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to import graph-owned graphics prefix"));
        discardRenderPackets();
        return;
    }

    const auto submitFrameRecoveryPacket = [&](const Core::QueueSubmissionToken* asyncWaitToken) -> bool {
        // Retire the accepted frame scope after a rejected packet and, when applicable, join accepted async work
        // without queue-family ownership repair.
        if(device.isDeviceLost()){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: frame recovery packet skipped because the graphics device is lost"));
            frameTimingTransaction.discard();
            return false;
        }
        NWB_ASSERT(!asyncWaitToken || asyncWaitToken->valid());

        const bool retireTiming = frameTimingTransaction.needsRetirement();
        if(retireTiming)
            frameTimingTransaction.prepareForRecovery();
        frameRecoveryCommandList->open();
        if(!frameRecoveryCommandList->hasCommandBuffer()){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: failed to record frame recovery packet"));
            frameTimingTransaction.discard();
            return false;
        }
        if(retireTiming && !frameTimingTransaction.recordEnd(*frameRecoveryCommandList)){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: failed to write frame recovery timing endpoint"));
            frameTimingTransaction.discard();
            return false;
        }
        frameRecoveryCommandList->close();
        if(!frameRecoveryCommandList->hasCommandBuffer()){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: failed to close frame recovery packet"));
            frameTimingTransaction.discard();
            return false;
        }

        Core::QueueSubmissionDesc recoverySubmitDesc;
        if(asyncWaitToken)
            recoverySubmitDesc.setWaitTokens(asyncWaitToken, 1u);
        Core::CommandList* recoveryCommandLists[] = { frameRecoveryCommandList };
        const Core::QueueSubmissionToken recoveryToken = device.executeCommandLists(
            recoveryCommandLists,
            1u,
            Core::RenderLane::Graphics,
            recoverySubmitDesc
        );
        if(!recoveryToken.valid()){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: frame recovery submission was rejected"));
            frameTimingTransaction.discard();
            return false;
        }
        if(retireTiming && !frameTimingTransaction.confirmEndSubmission(false)){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: failed to retire frame recovery timing query"));
            frameTimingTransaction.discard();
        }
        return true;
    };
    Core::QueueSubmissionToken prefixSubmissionToken;
    Core::QueueSubmissionToken shadowVisibilitySubmissionToken;
    Core::QueueSubmissionToken hardwareCausticsSubmissionToken;
    Core::QueueSubmissionToken softwareCausticsSubmissionToken;
    Core::QueueSubmissionToken surfelGiSubmissionToken;
    Core::QueueSubmissionToken avboitPreSubmissionToken;
    Core::QueueSubmissionToken avboitDepthWarpSubmissionToken;
    Core::QueueSubmissionToken avboitExtinctionSubmissionToken;
    Core::QueueSubmissionToken avboitIntegrationSubmissionToken;
    Core::QueueSubmissionToken avboitAccumulationSubmissionToken;
    Core::QueueSubmissionToken avboitFinalSubmissionToken;
    Core::QueueSubmissionToken deferredLightingSubmissionToken;
    Core::QueueSubmissionToken deferredCompositeSubmissionToken;
    const auto recoverPendingFrameSubmission = [&]() -> bool {
        const Core::QueueSubmissionToken* asyncWaitToken = nullptr;
        if(
            shadowVisibilitySubmissionToken.valid()
            && shadowVisibilitySubmissionToken.queue == Core::CommandQueue::Compute
        )
            asyncWaitToken = &shadowVisibilitySubmissionToken;
        if(
            softwareCausticsSubmissionToken.valid()
            && softwareCausticsSubmissionToken.queue == Core::CommandQueue::Compute
        )
            asyncWaitToken = &softwareCausticsSubmissionToken;
        if(
            surfelGiSubmissionToken.valid()
            && surfelGiSubmissionToken.queue == Core::CommandQueue::Compute
        )
            asyncWaitToken = &surfelGiSubmissionToken;
        if(
            avboitDepthWarpSubmissionToken.valid()
            && avboitDepthWarpSubmissionToken.queue == Core::CommandQueue::Compute
        )
            asyncWaitToken = &avboitDepthWarpSubmissionToken;
        if(
            avboitIntegrationSubmissionToken.valid()
            && avboitIntegrationSubmissionToken.queue == Core::CommandQueue::Compute
        )
            asyncWaitToken = &avboitIntegrationSubmissionToken;
        if(
            deferredLightingSubmissionToken.valid()
            && deferredLightingSubmissionToken.queue == Core::CommandQueue::Compute
        )
            asyncWaitToken = &deferredLightingSubmissionToken;
        if(
            deferredCompositeSubmissionToken.valid()
            && deferredCompositeSubmissionToken.queue == Core::CommandQueue::Compute
        )
            asyncWaitToken = &deferredCompositeSubmissionToken;
        return (asyncWaitToken || frameTimingTransaction.needsRetirement())
            ? submitFrameRecoveryPacket(asyncWaitToken)
            : true
        ;
    };
    const auto failFrameRenderRecovery = [&](){
        if(m_frameRenderRecoveryFailed)
            return;
        m_frameRenderRecoveryFailed = true;
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: cannot safely continue after an unresolved frame recovery submission; requesting device recreation"));
        // Defer device recreation until accepted work cannot be invalidated.
        m_graphics.requestDeviceRecreation();
    };

    const auto submitAvboitPacket = [&](
        const Core::GpuSubmissionPacketId packet,
        const Core::GpuTaskGraphExternalCompletionToken* const externalCompletionTokens,
        const usize externalCompletionTokenCount,
        Core::GpuTimingSubmissionTicket& timingTicket
    ) -> Core::QueueSubmissionToken {
        Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_TaskGraphArena);
        const Core::GpuTaskGraphSubmitter submitter(device);
        const bool accepted =
            m_avboitTaskGraphValid
            && packet.valid()
            && submitter.submitPacket(
                m_avboitTaskGraph,
                m_avboitCompiledGraph,
                m_avboitRecordedGraph,
                packet,
                externalCompletionTokens,
                externalCompletionTokenCount,
                m_avboitSubmissionTransaction,
                scratchArena,
                &timingTicket
            )
        ;
        return accepted ? m_avboitSubmissionTransaction.packetToken(packet) : Core::QueueSubmissionToken{};
    };
    const auto submitAvboitLightingAndComposite = [&]() -> bool {
        const Core::GpuTaskGraphExternalCompletionToken avboitPrefixCompletionToken{
            .completion = m_avboitPrefixCompletion,
            .token = prefixSubmissionToken,
        };
        avboitPreSubmissionToken =
            m_avboitPreTask.valid()
            && m_avboitPrefixCompletion.valid()
            ? submitAvboitPacket(
                avboitPrePacket,
                &avboitPrefixCompletionToken,
                1u,
                avboitPreTimingTicket
            )
            : Core::QueueSubmissionToken{}
        ;
        if(!avboitPreSubmissionToken.valid()){
            discardUnacceptedGraphPackets();
            discardTimingTickets();
            restoreAvboitCpuState();
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: graph-owned AVBOIT pre submission was rejected"));
            if(!recoverPendingFrameSubmission())
                failFrameRenderRecovery();
            return false;
        }
        avboitFinalSubmissionToken = avboitPreSubmissionToken;

        if(avboitUsesAsyncCompute){
            avboitDepthWarpSubmissionToken = submitAvboitPacket(
                avboitDepthWarpPacket,
                nullptr,
                0u,
                avboitDepthWarpTimingTicket
            );
            if(!avboitDepthWarpSubmissionToken.valid()){
                discardUnacceptedGraphPackets();
                discardTimingTickets();
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: graph-owned AVBOIT depth-warp submission was rejected"));
                if(!recoverPendingFrameSubmission())
                    failFrameRenderRecovery();
                return false;
            }

            avboitExtinctionSubmissionToken = submitAvboitPacket(
                avboitExtinctionPacket,
                nullptr,
                0u,
                avboitExtinctionTimingTicket
            );
            if(!avboitExtinctionSubmissionToken.valid()){
                discardUnacceptedGraphPackets();
                discardTimingTickets();
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: graph-owned AVBOIT extinction submission was rejected"));
                if(!recoverPendingFrameSubmission())
                    failFrameRenderRecovery();
                return false;
            }

            avboitIntegrationSubmissionToken = submitAvboitPacket(
                avboitIntegrationPacket,
                nullptr,
                0u,
                avboitIntegrationTimingTicket
            );
            if(!avboitIntegrationSubmissionToken.valid()){
                discardUnacceptedGraphPackets();
                discardTimingTickets();
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: graph-owned AVBOIT integration submission was rejected"));
                if(!recoverPendingFrameSubmission())
                    failFrameRenderRecovery();
                return false;
            }

            avboitAccumulationSubmissionToken = submitAvboitPacket(
                avboitAccumulationPacket,
                nullptr,
                0u,
                avboitAccumulationTimingTicket
            );
            if(!avboitAccumulationSubmissionToken.valid()){
                discardUnacceptedGraphPackets();
                discardTimingTickets();
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: graph-owned AVBOIT accumulation submission was rejected"));
                if(!recoverPendingFrameSubmission())
                    failFrameRenderRecovery();
                return false;
            }
            avboitFinalSubmissionToken = avboitAccumulationSubmissionToken;
        }

        // Deferred lighting now imports the graph-owned AVBOIT completion directly. The live path additionally
        // waits for surfel GI; the opt-in lagged path consumes the accepted history snapshot instead.
        const Core::GpuTaskGraphExternalCompletionToken lightingCompletionTokens[] = {
            Core::GpuTaskGraphExternalCompletionToken{
                .completion = m_deferredLightingAvboitCompletion,
                .token = avboitFinalSubmissionToken,
            },
            Core::GpuTaskGraphExternalCompletionToken{
                .completion = deferredLightingDependentCompletion,
                .token = laggedAsyncLightingSchedule
                    ? m_laggedLightingHistorySubmissionToken
                    : surfelGiSubmissionToken,
            },
        };
        Core::Alloc::ScratchArena lightingScratchArena(RendererArenaScope::s_TaskGraphArena);
        const Core::GpuTaskGraphSubmitter deferredLightingSubmitter(device);
        const bool deferredLightingAccepted =
            m_deferredLightingTaskGraphValid
            && m_deferredLightingTask.valid()
            && m_deferredLightingAvboitCompletion.valid()
            && deferredLightingDependentCompletion.valid()
            && deferredLightingSubmitter.submitPacket(
                m_deferredLightingTaskGraph,
                m_deferredLightingCompiledGraph,
                m_deferredLightingRecordedGraph,
                deferredLightingPacket,
                lightingCompletionTokens,
                LengthOf(lightingCompletionTokens),
                m_deferredLightingSubmissionTransaction,
                lightingScratchArena,
                &deferredLightingTimingTicket
            )
        ;
        deferredLightingSubmissionToken = deferredLightingAccepted
            ? m_deferredLightingSubmissionTransaction.packetToken(deferredLightingPacket)
            : Core::QueueSubmissionToken{}
        ;
        if(!deferredLightingSubmissionToken.valid()){
            discardUnacceptedGraphPackets();
            discardTimingTickets();
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: graph-owned deferred lighting submission was rejected"));
            if(!recoverPendingFrameSubmission())
                failFrameRenderRecovery();
            return false;
        }
        if(laggedAsyncLightingSchedule)
            deferredTargets.laggedLightingHistory.slotsUploaded = true;
        const bool lightingReturnStatesReady = !deferredLightingRunsOnCompute || laggedAsyncLightingSchedule || (
            m_shadowVisibilityReturnStateHandoff.buildTextureSubset(
                m_deferredLightingStateHandoff,
                deferredTargets.shadowVisibility.get()
            )
            // Bootstrap uses live caustics; active lagged mode uses the producer image directly.
            && m_causticIrradianceReturnStateHandoff.buildTextureSubset(
                m_deferredLightingStateHandoff,
                deferredTargets.causticIrradiance.get()
            )
            && m_surfelIrradianceReturnStateHandoff.buildTextureSubset(
                m_deferredLightingStateHandoff,
                deferredTargets.surfelIrradiance.get()
            )
        );
        if(!lightingReturnStatesReady){
            discardUnacceptedGraphPackets();
            discardTimingTickets();
            if(!recoverPendingFrameSubmission())
                failFrameRenderRecovery();
            // Lost post-lighting state leaves no safe producer layout.
            failFrameRenderRecovery();
            return false;
        }
        if(laggedAsyncLightingSchedule){
            reportLaggedLightingTransition(
                LaggedLightingReport::ActiveHistoryAccepted,
                deferredTargets.laggedLightingHistory.generation
            );
        }

        Core::Alloc::ScratchArena compositeScratchArena(RendererArenaScope::s_TaskGraphArena);
        const Core::GpuTaskGraphExternalCompletionToken lightingCompletionToken{
            .completion = m_deferredCompositeLightingCompletion,
            .token = deferredLightingSubmissionToken,
        };
        const Core::GpuTaskGraphSubmitter deferredCompositeSubmitter(device);
        const bool deferredCompositeAccepted =
            m_deferredCompositeTaskGraphValid
            && m_deferredCompositeTask.valid()
            && m_deferredCompositeLightingCompletion.valid()
            && deferredCompositeSubmitter.submitPacket(
                m_deferredCompositeTaskGraph,
                m_deferredCompositeCompiledGraph,
                m_deferredCompositeRecordedGraph,
                m_deferredCompositeCompiledGraph.packetForTask(m_deferredCompositeTask),
                &lightingCompletionToken,
                1u,
                m_deferredCompositeSubmissionTransaction,
                compositeScratchArena,
                &deferredCompositeTimingTicket
            )
        ;
        deferredCompositeSubmissionToken = deferredCompositeAccepted
            ? m_deferredCompositeSubmissionTransaction.packetToken(
                m_deferredCompositeCompiledGraph.packetForTask(m_deferredCompositeTask)
            )
            : Core::QueueSubmissionToken{}
        ;
        if(!deferredCompositeSubmissionToken.valid()){
            discardUnacceptedGraphPackets();
            discardTimingTickets();
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: graph-owned deferred composite submission was rejected"));
            if(!recoverPendingFrameSubmission())
                failFrameRenderRecovery();
            return false;
        }
        return true;
    };

    Core::QueueSubmissionToken finalPresentationSubmissionToken;
    const auto submitDeferredPresent = [&]() -> bool {
        Core::GpuTaskGraphExternalCompletionToken presentCompletionTokens[2] = {
            Core::GpuTaskGraphExternalCompletionToken{
                .completion = m_deferredPresentCompositeCompletion,
                .token = deferredCompositeSubmissionToken,
            },
        };
        usize presentCompletionCount = 1u;
        if(laggedAsyncLightingSchedule){
            presentCompletionTokens[presentCompletionCount++] = {
                .completion = m_deferredPresentSurfelGiCompletion,
                .token = surfelGiSubmissionToken,
            };
        }
        Core::Alloc::ScratchArena presentScratchArena(RendererArenaScope::s_TaskGraphArena);
        const Core::GpuTaskGraphSubmitter deferredPresentSubmitter(device);
        const bool deferredPresentAccepted =
            m_deferredPresentTaskGraphValid
            && m_deferredPresentTask.valid()
            && m_deferredPresentCompositeCompletion.valid()
            && (!laggedAsyncLightingSchedule || m_deferredPresentSurfelGiCompletion.valid())
            && deferredPresentSubmitter.submitPacket(
                m_deferredPresentTaskGraph,
                m_deferredPresentCompiledGraph,
                m_deferredPresentRecordedGraph,
                deferredPresentPacket,
                presentCompletionTokens,
                presentCompletionCount,
                m_deferredPresentSubmissionTransaction,
                presentScratchArena,
                &deferredPresentTimingTicket
            )
        ;
        finalPresentationSubmissionToken = deferredPresentAccepted
            ? m_deferredPresentSubmissionTransaction.packetToken(deferredPresentPacket)
            : Core::QueueSubmissionToken{}
        ;
        if(!finalPresentationSubmissionToken.valid()){
            discardUnacceptedGraphPackets();
            discardTimingTickets();
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: graph-owned deferred present submission was rejected"));
            if(!recoverPendingFrameSubmission())
                failFrameRenderRecovery();
            return false;
        }
        if(!frameTimingTransaction.confirmEndSubmission(true)){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: failed to confirm frame critical-path timing"));
            frameTimingTransaction.discard();
        }
        if(!dedicatedAsyncCompute && m_frameLaggedAsyncLightingEnabled){
            reportLaggedLightingTransition(
                LaggedLightingReport::NoDedicatedAsyncCompute,
                deferredTargets.laggedLightingHistory.generation
            );
        }
        else if(m_laggedLightingCurrentFrameAcceptancePending){
            reportLaggedLightingTransition(
                LaggedLightingReport::CurrentFrameAccepted,
                deferredTargets.laggedLightingHistory.generation
            );
            m_laggedLightingCurrentFrameAcceptancePending = false;
        }
        return true;
    };

    // The graph owns the prefix packet and publishes the completion consumed by every subsequent producer.
    {
        Core::Alloc::ScratchArena graphicsPrefixScratchArena(RendererArenaScope::s_TaskGraphArena);
        const Core::GpuTaskGraphSubmitter graphicsPrefixSubmitter(device);
        const bool graphicsPrefixAccepted =
            m_graphicsPrefixTaskGraphValid
            && m_graphicsPrefixTask.valid()
            && graphicsPrefixSubmitter.submitPacket(
                m_graphicsPrefixTaskGraph,
                m_graphicsPrefixCompiledGraph,
                m_graphicsPrefixRecordedGraph,
                graphicsPrefixPacket,
                nullptr,
                0u,
                m_graphicsPrefixSubmissionTransaction,
                graphicsPrefixScratchArena,
                &graphicsPrefixTimingTicket
            )
        ;
        prefixSubmissionToken = graphicsPrefixAccepted
            ? m_graphicsPrefixSubmissionTransaction.packetToken(graphicsPrefixPacket)
            : Core::QueueSubmissionToken{}
        ;
        if(!prefixSubmissionToken.valid()){
            discardRenderPackets();
            return;
        }
        frameTimingTransaction.confirmBeginSubmission();
        // The graph-owned shadow packet consumes the accepted prefix token on either physical transport.
        {
            Core::Alloc::ScratchArena shadowVisibilityScratchArena(RendererArenaScope::s_TaskGraphArena);
            const Core::GpuTaskGraphExternalCompletionToken prefixCompletionToken{
                .completion = m_shadowVisibilityPrefixCompletion,
                .token = prefixSubmissionToken,
            };
            const Core::GpuTaskGraphSubmitter shadowVisibilitySubmitter(device);
            const bool shadowVisibilityAccepted =
                m_shadowVisibilityTaskGraphValid
                && m_shadowVisibilityTask.valid()
                && m_shadowVisibilityPrefixCompletion.valid()
                && shadowVisibilitySubmitter.submitPacket(
                    m_shadowVisibilityTaskGraph,
                    m_shadowVisibilityCompiledGraph,
                    m_shadowVisibilityRecordedGraph,
                    shadowVisibilityPacket,
                    &prefixCompletionToken,
                    1u,
                    m_shadowVisibilitySubmissionTransaction,
                    shadowVisibilityScratchArena,
                    &shadowVisibilityTimingTicket
                )
            ;
            shadowVisibilitySubmissionToken = shadowVisibilityAccepted
                ? m_shadowVisibilitySubmissionTransaction.packetToken(shadowVisibilityPacket)
                : Core::QueueSubmissionToken{}
            ;
            if(!shadowVisibilitySubmissionToken.valid()){
                discardUnacceptedGraphPackets();
                discardTimingTickets();
                restoreShadowCpuState();
                restorePostGbufferEffectsCpuState();
                m_raytracingSystem.discardSoftShadowTemporalHistory();
                resetRejectedShadowVisibilityStateHandoffs();
                if(!recoverPendingFrameSubmission())
                    failFrameRenderRecovery();
                return;
            }

            if(shadowVisibilityRunsOnCompute){
                // Retain only private compute scratch; shared inputs come from next frame's prefix.
                // Retain accepted producer state for recovery after later rejection.
                const bool producerReturnStatesReady =
                    m_shadowVisibilityReturnStateHandoff.buildTextureSubset(
                        m_shadowVisibilityStateHandoff,
                        deferredTargets.shadowVisibility.get()
                    )
                ;
                if(!producerReturnStatesReady){
                    discardUnacceptedGraphPackets();
                    discardTimingTickets();
                    // Both caustics variants have recorded but have not yet accepted.
                    restorePostGbufferEffectsCpuState();
                    m_raytracingSystem.finalizeSoftShadowTemporalHistory(deferredTargets);
                    if(!recoverPendingFrameSubmission())
                        failFrameRenderRecovery();
                    // Missing accepted producer state leaves no safe next layout.
                    failFrameRenderRecovery();
                    return;
                }
                Core::Texture* const shadowComputeScratchTextures[] = {
                    deferredTargets.shadowCoarseTransmittance.get(),
                    deferredTargets.shadowSoftHalfA.get(),
                    deferredTargets.shadowSoftHalfB.get(),
                    deferredTargets.shadowSoftGeometry.get(),
                    deferredTargets.shadowSoftGeometryPrev.get(),
                    deferredTargets.shadowHistA.get(),
                    deferredTargets.shadowHistB.get(),
                    deferredTargets.shadowMomentsA.get(),
                    deferredTargets.shadowMomentsB.get(),
                    deferredTargets.transparentSoftHalf.get(),
                    deferredTargets.transparentHistA.get(),
                    deferredTargets.transparentHistB.get(),
                    deferredTargets.transparentMomentsA.get(),
                    deferredTargets.transparentMomentsB.get(),
                };
                Core::Buffer* const shadowComputeScratchBuffers[] = {
                    m_rayTracingState.m_swShadowEdgeStatsBuffer.get(),
                    m_rayTracingState.m_swShadowEdgeStatsReadback.get(),
                    m_rayTracingState.m_swShadowEdgeCounterBuffer.get(),
                    m_rayTracingState.m_swShadowEdgeListBuffer.get(),
                    m_rayTracingState.m_swShadowIndirectArgsBuffer.get(),
                };
                if(!m_shadowComputePersistentStateHandoff.buildResourceSubset(
                    m_shadowVisibilityStateHandoff,
                    shadowComputeScratchTextures,
                    LengthOf(shadowComputeScratchTextures),
                    shadowComputeScratchBuffers,
                    LengthOf(shadowComputeScratchBuffers)
                )){
                    discardUnacceptedGraphPackets();
                    discardTimingTickets();
                    // Both caustics variants have recorded but have not yet accepted.
                    restorePostGbufferEffectsCpuState();
                    m_raytracingSystem.finalizeSoftShadowTemporalHistory(deferredTargets);
                    recoverPendingFrameSubmission();
                    // Missing compute scratch leaves no safe layout restoration.
                    failFrameRenderRecovery();
                    return;
                }
            }

            m_raytracingSystem.finalizeSoftShadowTemporalHistory(deferredTargets);
            if(hardwareShadowSupported){
                Core::GpuTaskGraphExternalCompletionToken hardwareCausticsCompletionTokens[2] = {
                    Core::GpuTaskGraphExternalCompletionToken{
                        .completion = m_hardwareCausticsPrefixCompletion,
                        .token = prefixSubmissionToken,
                    },
                };
                usize hardwareCausticsCompletionCount = 1u;
                if(laggedAsyncLightingSchedule){
                    hardwareCausticsCompletionTokens[hardwareCausticsCompletionCount++] = {
                        .completion = m_hardwareCausticsLaggedHistoryCompletion,
                        .token = m_laggedLightingHistorySubmissionToken,
                    };
                }
                Core::Alloc::ScratchArena hardwareCausticsScratchArena(RendererArenaScope::s_TaskGraphArena);
                const Core::GpuTaskGraphSubmitter hardwareCausticsSubmitter(device);
                const bool hardwareCausticsAccepted =
                    m_hardwareCausticsTaskGraphValid
                    && m_hardwareCausticsTask.valid()
                    && m_hardwareCausticsPrefixCompletion.valid()
                    && (!laggedAsyncLightingSchedule || m_hardwareCausticsLaggedHistoryCompletion.valid())
                    && hardwareCausticsSubmitter.submitPacket(
                        m_hardwareCausticsTaskGraph,
                        m_hardwareCausticsCompiledGraph,
                        m_hardwareCausticsRecordedGraph,
                        hardwareCausticsPacket,
                        hardwareCausticsCompletionTokens,
                        hardwareCausticsCompletionCount,
                        m_hardwareCausticsSubmissionTransaction,
                        hardwareCausticsScratchArena,
                        &hardwareCausticsTimingTicket
                    )
                ;
                hardwareCausticsSubmissionToken = hardwareCausticsAccepted
                    ? m_hardwareCausticsSubmissionTransaction.packetToken(hardwareCausticsPacket)
                    : Core::QueueSubmissionToken{}
                ;
                if(!hardwareCausticsSubmissionToken.valid()){
                    discardUnacceptedGraphPackets();
                    discardTimingTickets();
                    restorePostGbufferEffectsCpuState();
                    resetAbandonedFrameStateHandoffs();
                    NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: graph-owned hardware caustics submission was rejected"));
                    if(!recoverPendingFrameSubmission())
                        failFrameRenderRecovery();
                    return;
                }
            }
            if(!hardwareShadowSupported){
                Core::Alloc::ScratchArena softwareCausticsScratchArena(RendererArenaScope::s_TaskGraphArena);
                const Core::GpuTaskGraphExternalCompletionToken shadowVisibilityCompletionToken{
                    .completion = m_softwareCausticsShadowVisibilityCompletion,
                    .token = shadowVisibilitySubmissionToken,
                };
                const Core::GpuTaskGraphSubmitter softwareCausticsSubmitter(device);
                const bool softwareCausticsAccepted =
                    m_softwareCausticsTaskGraphValid
                    && m_softwareCausticsTask.valid()
                    && m_softwareCausticsShadowVisibilityCompletion.valid()
                    && softwareCausticsSubmitter.submitPacket(
                        m_softwareCausticsTaskGraph,
                        m_softwareCausticsCompiledGraph,
                        m_softwareCausticsRecordedGraph,
                        softwareCausticsPacket,
                        &shadowVisibilityCompletionToken,
                        1u,
                        m_softwareCausticsSubmissionTransaction,
                        softwareCausticsScratchArena,
                        &softwareCausticsTimingTicket
                    )
                ;
                softwareCausticsSubmissionToken = softwareCausticsAccepted
                    ? m_softwareCausticsSubmissionTransaction.packetToken(softwareCausticsPacket)
                    : Core::QueueSubmissionToken{}
                ;
                if(!softwareCausticsSubmissionToken.valid()){
                    discardUnacceptedGraphPackets();
                    discardTimingTickets();
                    restoreCausticsCpuState();
                    restoreSurfelGiCpuState();
                    restoreAvboitCpuState();
                    NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: graph-owned software caustics submission was rejected"));
                    if(!recoverPendingFrameSubmission())
                        failFrameRenderRecovery();
                    return;
                }

                if(softwareCausticsRunsOnCompute){
                    const bool softwareCausticsStateReady =
                        m_causticIrradianceReturnStateHandoff.buildTextureSubset(
                            m_causticsStateHandoff,
                            deferredTargets.causticIrradiance.get()
                        )
                    ;
                    if(!softwareCausticsStateReady){
                        discardUnacceptedGraphPackets();
                        discardTimingTickets();
                        restoreSurfelGiCpuState();
                        restoreAvboitCpuState();
                        if(!recoverPendingFrameSubmission())
                            failFrameRenderRecovery();
                        // An accepted Compute producer without retained output state cannot safely span frames.
                        failFrameRenderRecovery();
                        return;
                    }

                    Core::Texture* const causticsComputeScratchTextures[] = {
                        deferredTargets.causticAccumulator.get(),
                        deferredTargets.causticHistory.get(),
                        deferredTargets.causticResolveHalf.get(),
                        deferredTargets.causticResolveGeometry.get(),
                    };
                    if(!m_causticsComputePersistentStateHandoff.buildResourceSubset(
                        m_causticsStateHandoff,
                        causticsComputeScratchTextures,
                        LengthOf(causticsComputeScratchTextures),
                        nullptr,
                        0u
                    )){
                        discardUnacceptedGraphPackets();
                        discardTimingTickets();
                        restoreSurfelGiCpuState();
                        restoreAvboitCpuState();
                        if(!recoverPendingFrameSubmission())
                            failFrameRenderRecovery();
                        failFrameRenderRecovery();
                        return;
                    }
                }
            }

            const Core::QueueSubmissionToken surfelGiProducerToken = hardwareShadowSupported
                ? shadowVisibilitySubmissionToken
                : softwareCausticsSubmissionToken
            ;
            Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_TaskGraphArena);
            const Core::GpuTaskGraphExternalCompletionToken effectsCompletionToken{
                .completion = m_surfelGiEffectsCompletion,
                .token = surfelGiProducerToken,
            };
            const Core::GpuTaskGraphSubmitter surfelGiSubmitter(device);
            const bool surfelGiAccepted =
                m_surfelGiTaskGraphValid
                && m_surfelGiTask.valid()
                && m_surfelGiEffectsCompletion.valid()
                && surfelGiSubmitter.submitPacket(
                    m_surfelGiTaskGraph,
                    m_surfelGiCompiledGraph,
                    m_surfelGiRecordedGraph,
                    surfelGiPacket,
                    &effectsCompletionToken,
                    1u,
                    m_surfelGiSubmissionTransaction,
                    scratchArena,
                    &surfelGiTimingTicket
                )
            ;
            surfelGiSubmissionToken = surfelGiAccepted
                ? m_surfelGiSubmissionTransaction.packetToken(surfelGiPacket)
                : Core::QueueSubmissionToken{}
            ;
            if(!surfelGiSubmissionToken.valid()){
                discardUnacceptedGraphPackets();
                discardTimingTickets();
                restoreSurfelGiCpuState();
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: graph-owned surfel GI submission was rejected"));
                if(!recoverPendingFrameSubmission())
                    failFrameRenderRecovery();
                return;
            }
            if(!m_surfelIrradianceReturnStateHandoff.buildTextureSubset(
                m_surfelGiStateHandoff,
                deferredTargets.surfelIrradiance.get()
            )){
                discardUnacceptedGraphPackets();
                discardTimingTickets();
                if(!recoverPendingFrameSubmission())
                    failFrameRenderRecovery();
                // An accepted graph producer without a retained state cannot safely feed a later frame.
                failFrameRenderRecovery();
                return;
            }

            if(surfelGiRunsOnCompute){
                Core::Texture* const surfelGiComputeScratchTextures[] = {
                    deferredTargets.surfelIrradianceHalf.get(),
                };
                Core::Buffer* const surfelGiComputeScratchBuffers[] = {
                    m_rayTracingState.m_surfelPoolBuffer.get(),
                    m_rayTracingState.m_surfelCellHeadBuffer.get(),
                    m_rayTracingState.m_surfelCounterBuffer.get(),
                    m_rayTracingState.m_surfelTraceIndirectArgsBuffer.get(),
                    m_rayTracingState.m_surfelFreeListBuffer.get(),
                    m_rayTracingState.m_surfelPoolSnapshotBuffer.get(),
                    m_rayTracingState.m_surfelCellHeadSnapshotBuffer.get(),
                    m_rayTracingState.m_surfelCounterReadback.get(),
                };
                if(!m_surfelGiComputePersistentStateHandoff.buildResourceSubset(
                    m_surfelGiStateHandoff,
                    surfelGiComputeScratchTextures,
                    LengthOf(surfelGiComputeScratchTextures),
                    surfelGiComputeScratchBuffers,
                    LengthOf(surfelGiComputeScratchBuffers)
                )){
                    discardUnacceptedGraphPackets();
                    discardTimingTickets();
                    if(!recoverPendingFrameSubmission())
                        failFrameRenderRecovery();
                    failFrameRenderRecovery();
                    return;
                }
            }

        }
        if(!submitAvboitLightingAndComposite() || !submitDeferredPresent())
            return;
    }

    if(captureLaggedLightingHistory){
        // The graph task waits on the accepted final presentation token.  Manual state handoff remains only until
        // compiler-produced packet state seeds replace the renderer's existing transition bridge.
        const Core::CommandListResourceStateHandoff* const causticHistoryCopySource = laggedAsyncLightingSchedule
            ? &m_causticIrradianceLightingStateHandoff
            : &m_causticIrradianceReturnStateHandoff
        ;
        const Core::CommandListResourceStateHandoff* const historyCopyBranches[] = {
            causticHistoryCopySource,
            &m_surfelIrradianceReturnStateHandoff,
        };
        const bool historyCopyInputReady = m_laggedLightingHistoryCopyInputStateHandoff.buildFanIn(
            m_shadowVisibilityReturnStateHandoff,
            historyCopyBranches,
            LengthOf(historyCopyBranches)
        );
        if(!historyCopyInputReady){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: lagged lighting-history capture skipped because its source state was unavailable"));
            invalidateLaggedLightingHistorySubmission();
        }
        else if(
            !m_laggedLightingHistoryTaskGraphValid
            || !finalPresentationSubmissionToken.valid()
            || !m_laggedLightingHistoryTask.valid()
            || !m_laggedLightingPresentationCompletion.valid()
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: lagged lighting-history graph was unavailable; reverting to current-frame lighting"));
            invalidateLaggedLightingHistorySubmission();
            resetLaggedLightingHistoryCopyStateHandoffs();
        }
        else{
            const Core::GpuSubmissionPacketId historyPacket = m_laggedLightingHistoryCompiledGraph.packetForTask(
                m_laggedLightingHistoryTask
            );
            Core::GpuNativePacketRecorder recorder(device);
            const Core::GpuNativePacketRecordDesc recordDesc{
                .packet = historyPacket,
                .initialStates = &m_laggedLightingHistoryCopyInputStateHandoff,
                .finalStates = &m_laggedLightingHistoryCopyStateHandoff,
                .applyCompiledBarriers = true,
            };
            const bool historyCopyRecorded = historyPacket.valid()
                && recorder.recordPacket(
                    m_laggedLightingHistoryTaskGraph,
                    m_laggedLightingHistoryCompiledGraph,
                    recordDesc,
                    m_laggedLightingHistoryRecordedGraph
                )
                && m_laggedLightingHistoryCopyStateHandoff.valid()
            ;
            if(!historyCopyRecorded){
                m_laggedLightingHistorySubmissionTransaction.discardUnaccepted(
                    m_laggedLightingHistoryTaskGraph,
                    m_laggedLightingHistoryCompiledGraph
                );
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: failed to record graph-owned lagged lighting-history capture; reverting to current-frame lighting"));
                invalidateLaggedLightingHistorySubmission();
                resetLaggedLightingHistoryCopyStateHandoffs();
            }
            else{
                Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_TaskGraphArena);
                const Core::GpuTaskGraphExternalCompletionToken completionToken{
                    .completion = m_laggedLightingPresentationCompletion,
                    .token = finalPresentationSubmissionToken,
                };
                const Core::GpuTaskGraphSubmitter submitter(device);
                const bool historyCopyAccepted = submitter.submitPacket(
                    m_laggedLightingHistoryTaskGraph,
                    m_laggedLightingHistoryCompiledGraph,
                    m_laggedLightingHistoryRecordedGraph,
                    historyPacket,
                    &completionToken,
                    1u,
                    m_laggedLightingHistorySubmissionTransaction,
                    scratchArena
                );
                const Core::QueueSubmissionToken historyCopySubmissionToken = historyCopyAccepted
                    ? m_laggedLightingHistorySubmissionTransaction.packetToken(historyPacket)
                    : Core::QueueSubmissionToken{}
                ;
                if(!historyCopySubmissionToken.valid()){
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: graph-owned lagged lighting-history capture submission was rejected; reverting to current-frame lighting"));
                    invalidateLaggedLightingHistorySubmission();
                    resetLaggedLightingHistoryCopyStateHandoffs();
                }
                else{
                    const bool historyCopyReturnStatesReady =
                        m_shadowVisibilityReturnStateHandoff.buildTextureSubset(
                            m_laggedLightingHistoryCopyStateHandoff,
                            deferredTargets.shadowVisibility.get()
                        )
                        && m_causticIrradianceReturnStateHandoff.buildTextureSubset(
                            m_laggedLightingHistoryCopyStateHandoff,
                            deferredTargets.causticIrradiance.get()
                        )
                        && m_surfelIrradianceReturnStateHandoff.buildTextureSubset(
                            m_laggedLightingHistoryCopyStateHandoff,
                            deferredTargets.surfelIrradiance.get()
                        )
                    ;
                    if(!historyCopyReturnStatesReady){
                        if(!submitFrameRecoveryPacket(&historyCopySubmissionToken))
                            failFrameRenderRecovery();
                        // Lost retained copy state leaves no safe producer layout.
                        failFrameRenderRecovery();
                        return;
                    }

                    // The task's accepted hook publishes this exact token.  Keep the assertion close to the handoff
                    // so a future lifecycle change cannot accidentally reintroduce a renderer-side publication path.
                    NWB_ASSERT(
                        m_laggedLightingHistorySubmissionToken.queue == historyCopySubmissionToken.queue
                        && m_laggedLightingHistorySubmissionToken.value == historyCopySubmissionToken.value
                    );
                    if(!laggedAsyncLightingSchedule){
                        reportLaggedLightingTransition(
                            LaggedLightingReport::BootstrapAccepted,
                            deferredTargets.laggedLightingHistory.generation
                        );
                    }
                }
            }
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

