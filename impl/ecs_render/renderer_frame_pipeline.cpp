// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"
#include "renderer_frame_pipeline.h"
#include <impl/ecs_scene/components.adhd.h>
#include <impl/ecs_render/execute/frame_diagnostics.adhd.h>
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
#include <core/graphics/frame_graph_nodes.h>
#include <impl/ecs_render/renderer_frame_pipeline.h>
#include <impl/ecs_render/material/task_graph_object_geometry_cache.h>
#include <impl/ecs_render/reflection/history_planning.adhd.h>
#include <impl/ecs_render/raytrace/task_graph_scene_resources.h>
#include <impl/ecs_render/optics/task_graph_optical_scene_upload.h>
#include <impl/ecs_render/reflection/task_graph_reflection.h>
#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/deferred/task_graph_suffix_builder.h>
#include <impl/ecs_render/deferred/graph_resource_import_builder.h>
#include <impl/ecs_render/deferred/lighting_stage_builder.h>
#include <impl/ecs_render/deferred/frame_tail_builder.h>
#include <impl/ecs_render/caustic/hardware_caustics_stage_builder.h>
#include <impl/ecs_render/raytrace/frame_graph_trace_geometry.h>
#include <impl/ecs_render/avboit/frame_graph_avboit_stage.h>
#include <impl/ecs_render/reflection/frame_graph_reflection_resolve.h>
#include <impl/ecs_render/refraction/task_graph_refraction_resolve.h>
#include <impl/ecs_render/deferred/import_types.adhd.h>
#include <impl/ecs_render/gi/quality_settings.adhd.h>
#include <global/hash_utils.h>
#include <global/timer.h>
#include <core/task/gpu/frame_timing_begin_task.h>
#include <impl/ecs_render/deferred/graphics_prefix_graph_coordinator.h>
#include <impl/ecs_render/csg/csg_system.h>
#include <impl/ecs_render/deferred/deferred_system.h>
#include <impl/ecs_render/execute/types.adhd.h>
#include <impl/ecs_render/material/material_system.h>
#include <impl/ecs_render/mesh/mesh_system.h>
#include <impl/ecs_render/raytrace/raytracing_system.h>
#include <impl/ecs_render/deferred/post_gbuffer_normalize_builder.h>
#include <impl/ecs_render/shared/task_graph_draw_snapshots.h>
#include <impl/ecs_render/kernel/task_graph_queue_lookup.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/deferred/task_graph_prefix_tasks.h>
#include <impl/ecs_render/deferred/opaque_upload_chain_builder.h>
#include <impl/ecs_render/csg/opaque_csg_interval_clear_builder.h>
#include <impl/ecs_render/deferred/opaque_gbuffer_material_resources.h>
#include <impl/ecs_render/deferred/opaque_gbuffer_resource_uses.h>
#include <impl/ecs_render/deferred/opaque_compute_emulation_planner.h>
#include <impl/ecs_render/deferred/opaque_compute_emulation_producers.h>
#include <impl/ecs_render/csg/opaque_csg_interval_stage.h>
#include <impl/ecs_render/deferred/opaque_shared_compute_emulation_chain.h>
#include <impl/ecs_render/deferred/prefix_scene_upload_builder.h>
#include <impl/ecs_render/deferred/task_graph_gbuffer_task.h>
#include <impl/ecs_render/material/task_graph_compute_emulation_plan.h>
#include <impl/ecs_render/material/task_graph_opaque_compute_emulation_plan.h>
#include <impl/ecs_render/csg/task_graph_opaque_compute_emulation_plan.h>
#include <impl/ecs_render/mesh/task_graph_prefix_tasks.h>
#include <impl/ecs_render/material/task_graph_opaque_compute_tasks.h>
#include <impl/ecs_render/csg/task_graph_opaque_compute_tasks.h>
#include <impl/ecs_render/csg/task_graph_opaque_interval_tasks.h>
#include <core/task/gpu/capture/command_ir.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RendererSystem::RendererSystem(Core::Alloc::GlobalArena& arena, Core::ECS::World& world, Core::GraphicsRuntime& graphics, Core::Assets::AssetManager& assetManager, RendererShaderPathResolveCallback shaderPathResolver)
    : Core::ECS::ISystem(arena)
    , Core::IRenderPass(graphics)
    , m_pipeline(MakeNotNullUnique(Core::MakeGlobalUnique<RendererFramePipeline>(arena, arena, world, graphics, assetManager, Move(shaderPathResolver))))
{
    readAccess<NWB::Impl::Scene::ActiveCameraComponent>();
    readAccess<NWB::Impl::Scene::TransformComponent>();
    readAccess<NWB::Impl::Scene::CameraComponent>();
    readAccess<RendererComponent>();
    readAccess<MaterialInstanceComponent>();
    readAccess<CsgReceiverComponent>();
    readAccess<StaticCsgMeshComponent>();
    readAccess<SkinnedCsgMeshComponent>();
    readAccess<CsgCutterComponent>();
}

RendererSystem::~RendererSystem()noexcept = default;

b8 RendererSystem::validateResources(const u32 width, const u32 height, const u32 sampleCount){
    return m_pipeline->validateResources(width, height, sampleCount);
}

void RendererSystem::invalidateResources(){
    m_pipeline->invalidateResources();
}

Expected<void, Core::CpuTaskAdmissionError::Enum> RendererSystem::update(Core::ECS::World& world, const f32 delta)noexcept{
    m_pipeline->update(world, delta);
    return {};
}

b8 RendererSystem::prepareResources(Core::Framebuffer* framebuffer){
    return m_pipeline->prepareResources(framebuffer);
}

void RendererSystem::render(Core::Framebuffer* framebuffer){
    m_pipeline->render(framebuffer);
}

b8 RendererSystem::appendFrameGraph(Core::Telemetry::FrameGraphBuilder& builder){
    return m_pipeline->appendFrameGraph(builder);
}

void RendererSystem::setActiveSceneSubsystems(const ActiveSceneSubsystems* subsystems)noexcept{
    m_pipeline->setActiveSceneSubsystems(subsystems);
}

void RendererSystem::setRefractionEnabled(const b8 enabled)noexcept{
    m_pipeline->setRefractionEnabled(enabled);
}

void RendererSystem::setRefractionHardwareTracingEnabled(const b8 enabled)noexcept{
    m_pipeline->setRefractionHardwareTracingEnabled(enabled);
}

void RendererSystem::setRefractionDiagnosticsEnabled(const b8 enabled)noexcept{
    m_pipeline->setRefractionDiagnosticsEnabled(enabled);
}

Expected<RefractionStatistics> RendererSystem::tryGetLatestRefractionStatistics()const noexcept{
    return m_pipeline->tryGetLatestRefractionStatistics();
}

b8 RendererSystem::setCausticQualitySettings(const CausticQualitySettings& settings)noexcept{
    return m_pipeline->setCausticQualitySettings(settings);
}

b8 RendererSystem::setSurfelGiQualitySettings(const SurfelGiQualitySettings& settings)noexcept{
    return m_pipeline->setSurfelGiQualitySettings(settings);
}

b8 RendererSystem::setSoftwareShadowSettings(const SoftwareShadowSettings& settings)noexcept{
    return m_pipeline->setSoftwareShadowSettings(settings);
}

b8 RendererSystem::setShadowQualitySettings(const ShadowQualitySettings& settings)noexcept{
    return m_pipeline->setShadowQualitySettings(settings);
}

b8 RendererSystem::setReflectionSettings(const ReflectionSettings& settings)noexcept{
    return m_pipeline->setReflectionSettings(settings);
}

Expected<ReflectionStatistics> RendererSystem::tryGetLatestReflectionStatistics()const noexcept{
    return m_pipeline->tryGetLatestReflectionStatistics();
}

b8 RendererSystem::setPresentationSettings(const PresentationSettings& settings)noexcept{
    return m_pipeline->setPresentationSettings(settings);
}

void RendererSystem::setFrameLaggedAsyncLightingEnabled(const b8 enabled)noexcept{
    m_pipeline->setFrameLaggedAsyncLightingEnabled(enabled);
}

Core::GpuTaskGraphRuntimeStatistics RendererSystem::deferredTaskGraphRuntimeStatistics()const noexcept{
    return m_pipeline->deferredTaskGraphRuntimeStatistics();
}


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


void RendererFramePipeline::buildDeferredLightingTaskGraph(
    Core::Alloc::ScratchArena& scratchArena,
    const RendererFrameGraphFeatures& features,
    const CsgFrameState& csgFrameState,
    const b8 clearAvboitTargets,
    const b8 hasTransparentRenderers,
    const b8 hasOpaqueCsgFrameWork,
    const f32 meshViewAspectRatio,
    const Core::AcquiredPresentationFrame& presentationFrame,
    const b8& asyncPrefixTimingSpansOnePacket,
    const Core::QueueSubmissionToken& surfelCounterReadbackCompletionToken,
    const Core::QueueSubmissionToken& laggedLightingHistoryReadReadyToken,
    const Core::QueueSubmissionToken& laggedLightingHistoryWriterDrainToken,
    const b8 includeLaggedLightingHistoryCapture,
    DeferredFrameTargets& deferredTargets,
    Core::GpuTimingFrameTransaction& frameTimingTransaction,
    Optional<Core::GpuTimingMeasure>& asyncPrefixTiming,
    Optional<Core::GpuTimingMeasure>& deferredClearTiming,
    GraphClearTimingRecordState& deferredClearTimingState,
    GraphClearTimingRecordState& opaqueCsgIntervalClearTimingState,
    Optional<Core::GpuTimingMeasure>& opaqueRegularSharedComputeEmulationTiming,
    Optional<Core::GpuTimingMeasure>& opaqueCsgIntervalSampleComputeEmulationTiming,
    Core::GpuTimingSubmissionTicket& shadowPrepareTimingTicket,
    const Span<Core::GpuTimingSubmissionTicket*> graphicsPrefixTimingTickets,
    Optional<Core::GpuTimingMeasure>& asyncFinalTiming,
    Core::GpuTimingSubmissionTicket& avboitPreTimingTicket,
    GraphClearTimingRecordState& avboitClearTimingState,
    GraphClearTimingRecordState& transparentCsgIntervalClearTimingState,
    Optional<Core::GpuTimingMeasure>& transparentCsgIntervalsTiming,
    Optional<Core::GpuTimingMeasure>& avboitOccupancyComputeEmulationTiming,
    Optional<Core::GpuTimingMeasure>& avboitExtinctionComputeEmulationTiming,
    Optional<Core::GpuTimingMeasure>& avboitAccumulationComputeEmulationTiming,
    Core::GpuTimingSubmissionTicket& avboitDepthWarpTimingTicket,
    Core::GpuTimingSubmissionTicket& avboitExtinctionTimingTicket,
    Core::GpuTimingSubmissionTicket& avboitIntegrationTimingTicket,
    Core::GpuTimingSubmissionTicket& avboitAccumulationTimingTicket,
    Core::GpuTimingSubmissionTicket& shadowVisibilityTimingTicket,
    Optional<Core::GpuTimingMeasure>& shadowVisibilityAsyncTiming,
    Optional<Core::GpuTimingMeasure>& shadowVisibilityTiming,
    Optional<Core::GpuTimingMeasure>& opaqueSoftResolveTiming,
    Optional<Core::GpuTimingMeasure>& transparentSoftResolveTiming,
    b8& shadowVisibilityOpaqueProduced,
    b8& shadowVisibilityTransparentTraceProduced,
    u32& shadowVisibilityOpaqueFrameIndex,
    Core::GpuTimingSubmissionTicket& softwareCausticsTimingTicket,
    Core::GpuTimingSubmissionTicket& surfelGiTimingTicket,
    Optional<Core::GpuTimingMeasure>& surfelGiAsyncTiming,
    Core::GpuTimingSubmissionTicket& hardwareCausticsTimingTicket,
    Optional<Core::GpuTimingMeasure>& causticPhotonTiming,
    Optional<Core::GpuTimingMeasure>& causticResolveTiming,
    Core::GpuTimingSubmissionTicket& lightingTimingTicket,
    Core::GpuTimingSubmissionTicket& compositeTimingTicket,
    Core::GpuTimingSubmissionTicket& presentTimingTicket
){
    using namespace RendererTaskGraphDetail;

    const RayTracingShadowPreparationResourceSnapshot rayTracingShadowResources = m_raytracingSystem.snapshotShadowPreparationResources();
    const RayTracingDeferredGraphResourceSnapshot rayTracingGraphResources = m_raytracingSystem.snapshotDeferredGraphResources();
    const RayTracingSurfelPersistentResourceSnapshot rayTracingSurfelResources = m_raytracingSystem.snapshotSurfelPersistentResources();

    m_deferredFrameTimingBeginTask = {};
    m_shadowPrepareGraph.resetShadowPrepareTaskState();
    m_graphicsPrefixGraph.m_graphicsPrefixCsgIntervalClearFirstTask = {};
    m_graphicsPrefixGraph.m_graphicsPrefixCsgIntervalClearTask = {};
    resetDeferredFrameTaskIdsAndFlags();
    resetDeferredTaskGraphRuntime();
    // Declaration runs between artifact discard and execution-time compilation.
    const Timer declarationBegin = TimerNow();
    if(!declareFrameTimingBeginTask(frameTimingTransaction))
        return;

    const auto& device = m_graphics.getDevice();
    const u32 graphicsFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Graphics);
    const u32 computeFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Compute);
    const b8 dedicatedAsyncCompute = computeFamilyIndex != Limit<u32>::s_Max && computeFamilyIndex != graphicsFamilyIndex;
    const b8 useLaggedLightingHistory = dedicatedAsyncCompute && features.frameLaggedAsyncLightingEnabled && features.laggedLightingHistoryReady && features.laggedLightingHistoryReadReady;
    const b8 declaresHardwareCaustics = features.hardwareCaustics;
    const b8 capturesLaggedLightingHistory = includeLaggedLightingHistoryCapture && dedicatedAsyncCompute && features.frameLaggedAsyncLightingEnabled;
    const DeferredLaggedLightingHistoryResources* const history = useLaggedLightingHistory ? &deferredTargets.laggedLightingHistory : nullptr;
    const DeferredLaggedLightingHistoryResources* const captureHistory = capturesLaggedLightingHistory ? &deferredTargets.laggedLightingHistory : nullptr;
    if(!presentationFrame.valid())
        return;
    const Core::Framebuffer& presentationFramebuffer = *presentationFrame.framebuffer;
    const Core::FramebufferDesc& presentationFramebufferDesc = presentationFramebuffer.getDescription();
    const DeferredLightingGraphResources deferredLightingResources = m_deferredSystem.lightingGraphResources();
    const ECSRenderDetail::MeshFrameBindingSnapshot frameBindings = m_meshSystem.meshFrameBindingSnapshot();
    const b8 retainRefractionCapPipeline = m_preparedRefractionActive && (csgFrameState.hasTransparentStaticWork || csgFrameState.hasTransparentSkinnedWork);
    const ECSRenderDetail::CsgGraphResourceSnapshot csgResources = csgFrameState.empty() ? ECSRenderDetail::CsgGraphResourceSnapshot{} : m_csgSystem.csgGraphResourceSnapshot(retainRefractionCapPipeline);
    const ECSRenderDetail::MeshViewBufferSnapshot& meshViewBufferSnapshot = frameBindings.meshView;
    if(
        !deferredTargets.valid()
        || !deferredTargets.bindless.valid()
        || !meshViewBufferSnapshot.valid()
        || (!csgFrameState.empty() && !frameBindings.bindingValid())
        || !deferredLightingResources.valid()
        || presentationFramebufferDesc.colorAttachments.size() != 1u
        || presentationFramebufferDesc.colorAttachments[0].texture != presentationFrame.backBuffer.texture.get()
        || hasTransparentRenderers != features.hasTransparentRenderers
        || (useLaggedLightingHistory && (!history || !history->valid()))
        || (capturesLaggedLightingHistory && (!captureHistory || !captureHistory->valid()))
    )
        return;

    FrameGraphTraceGeometryImporter traceGeometryImporter(m_deferredLightingTaskGraph, m_raytracingSystem, m_meshSystem);
    const auto traceGeometryResult = traceGeometryImporter.declare(scratchArena, rayTracingShadowResources);
    if(!traceGeometryResult)
        return;
    const FrameGraphTraceGeometryResult& traceGeometry = *traceGeometryResult;
    DeferredGraphResourceImportBuilder deferredGraphResourceImportBuilder(m_deferredLightingTaskGraph);
    auto deferredGraphResources = deferredGraphResourceImportBuilder.declare(
        DeferredGraphResourceImportInputs{
            .targets = deferredTargets,
            .lightingResources = deferredLightingResources,
            .frameBindings = frameBindings,
            .meshViewSnapshot = meshViewBufferSnapshot,
            .csgResources = csgResources,
            .rayTracingResources = rayTracingGraphResources,
            .history = history,
            .captureHistory = captureHistory,
            .clearAvboitTargets = clearAvboitTargets,
            .capturesLaggedLightingHistory = capturesLaggedLightingHistory,
            .hasCsgFrameWork = !csgFrameState.empty(),
        });
    if(!deferredGraphResources){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import deferred-lighting graph resources"));
        return;
    }
    const Core::GpuGraphResourceId albedo = deferredGraphResources->albedo;
    const Core::GpuGraphResourceId normal = deferredGraphResources->normal;
    const Core::GpuGraphResourceId worldPosition = deferredGraphResources->worldPosition;
    const Core::GpuGraphResourceId specularRoughness = deferredGraphResources->specularRoughness;
    const Core::GpuGraphResourceId depth = deferredGraphResources->depth;
    const Core::GpuGraphResourceId shadowVisibility = deferredGraphResources->shadowVisibility;
    const Core::GpuGraphResourceId causticIrradiance = deferredGraphResources->causticIrradiance;
    const Core::GpuGraphResourceId surfelIrradiance = deferredGraphResources->surfelIrradiance;
    const Core::GpuGraphResourceId currentShadowVisibility = deferredGraphResources->currentShadowVisibility;
    const Core::GpuGraphResourceId currentCausticIrradiance = deferredGraphResources->currentCausticIrradiance;
    const Core::GpuGraphResourceId currentSurfelIrradiance = deferredGraphResources->currentSurfelIrradiance;
    const Core::GpuGraphResourceId opaqueColor = deferredGraphResources->opaqueColor;
    const Core::GpuGraphResourceId sceneShading = deferredGraphResources->sceneShading;
    const Core::GpuGraphResourceId lights = deferredGraphResources->lights;
    const Core::GpuGraphResourceId meshView = deferredGraphResources->meshView;
    const Core::GpuGraphResourceId bindlessSlots = deferredGraphResources->bindlessSlots;
    const Core::GpuGraphResourceId currentBindlessSlots = deferredGraphResources->currentBindlessSlots;
    const Core::GpuGraphResourceId materialContextSlots = deferredGraphResources->materialContextSlots;
    const Core::GpuGraphResourceId historyCopyShadowVisibility = deferredGraphResources->historyCopyShadowVisibility;
    const Core::GpuGraphResourceId historyCopyCausticIrradiance = deferredGraphResources->historyCopyCausticIrradiance;
    const Core::GpuGraphResourceId historyCopySurfelIrradiance = deferredGraphResources->historyCopySurfelIrradiance;
    const Core::GpuGraphResourceId historyCopyDestinationShadowVisibility = deferredGraphResources->historyCopyDestinationShadowVisibility;
    const Core::GpuGraphResourceId historyCopyDestinationCausticIrradiance = deferredGraphResources->historyCopyDestinationCausticIrradiance;
    const Core::GpuGraphResourceId historyCopyDestinationSurfelIrradiance = deferredGraphResources->historyCopyDestinationSurfelIrradiance;
    const Core::GpuGraphResourceId avboitAccumColor = deferredGraphResources->avboitAccumColor;
    const Core::GpuGraphResourceId avboitAccumExtinction = deferredGraphResources->avboitAccumExtinction;
    const Core::GpuGraphResourceId refractionDepth = deferredGraphResources->refractionDepth;
    const Core::GpuGraphResourceId refractionNormalIor = deferredGraphResources->refractionNormalIor;
    const Core::GpuGraphResourceId refractionTintCoverage = deferredGraphResources->refractionTintCoverage;
    const Core::GpuGraphResourceId refractionInstance = deferredGraphResources->refractionInstance;
    const Core::GpuGraphResourceId refractionSpecularRoughness = deferredGraphResources->refractionSpecularRoughness;
    const Core::GpuGraphResourceId refractionResolve = deferredGraphResources->refractionResolve;
    const Core::GpuGraphResourceId avboitForegroundColor = deferredGraphResources->avboitForegroundColor;
    const Core::GpuGraphResourceId avboitForegroundExtinction = deferredGraphResources->avboitForegroundExtinction;

    const ShadowPrepareGraphContext shadowPrepareContext{
        .graph = m_deferredLightingTaskGraph,
        .graphics = m_graphics,
        .mesh = m_meshSystem,
        .rayTracing = m_raytracingSystem,
        .persistentState = m_shadowPreparePersistentState,
    };
    if(!m_shadowPrepareGraph.declareDeferredShadowPrepareTask(
        shadowPrepareContext,
        rayTracingShadowResources,
        rayTracingGraphResources,
        currentBindlessSlots,
        materialContextSlots,
        traceGeometry.traceGeometry,
        traceGeometry.softwareBvhBuildState,
        traceGeometry.softwareTraceResourcesPrepared,
        scratchArena,
        deferredTargets,
        shadowPrepareTimingTicket
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare shared shadow-preparation packet"));
        return;
    }
    const Core::GpuTaskId shadowPrepareHandoffTask = m_shadowPrepareGraph.m_deferredShadowPrepareAccelStructFinalizeTask.valid() ? m_shadowPrepareGraph.m_deferredShadowPrepareAccelStructFinalizeTask : m_shadowPrepareGraph.m_deferredShadowPrepareTask;

    const MeshViewBufferUpload meshViewUpload = m_meshSystem.prepareMeshViewBufferUpload(meshViewAspectRatio);
    const MeshViewGpuData& meshViewState = meshViewUpload.viewState;
    const b8 meshViewUploadRequired = meshViewUpload.uploadRequired;

    ReflectionSceneContentStamp reflectionContentStamp;
    reflectionContentStamp.view = ComputeFnv64Bytes(&meshViewState, sizeof(meshViewState));
    ObjectGeometryCacheGraph objectGeometry(m_deferredLightingTaskGraph, m_materialSystem, m_meshSystem, scratchArena);
    const GraphicsPrefixGraphContext graphicsPrefixContext{
        .arena = m_arena,
        .graphics = m_graphics,
        .graph = m_deferredLightingTaskGraph,
        .csg = m_csgSystem,
        .deferred = m_deferredSystem,
        .material = m_materialSystem,
        .mesh = m_meshSystem,
        .rayTracing = m_raytracingSystem,
        .shadowVisibilityTask = m_shadowVisibilityGraph.m_deferredShadowVisibilityTask,
    };
    auto graphicsPrefixResult = m_graphicsPrefixGraph.declareDeferredGraphicsPrefixTasks(
        graphicsPrefixContext,
        shadowPrepareHandoffTask,
        csgFrameState,
        frameBindings,
        csgResources,
        hasOpaqueCsgFrameWork,
        meshViewAspectRatio,
        meshViewState,
        meshViewUploadRequired,
        *deferredGraphResources,
        traceGeometry.traceGeometry,
        traceGeometry.shadowTraceGeometrySet,
        asyncPrefixTimingSpansOnePacket,
        scratchArena,
        deferredTargets,
        objectGeometry,
        asyncPrefixTiming,
        deferredClearTiming,
        deferredClearTimingState,
        opaqueCsgIntervalClearTimingState,
        opaqueRegularSharedComputeEmulationTiming,
        opaqueCsgIntervalSampleComputeEmulationTiming,
        graphicsPrefixTimingTickets
    );
    if(!graphicsPrefixResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred graphics-prefix packet"));
        return;
    }
    reflectionContentStamp.lighting = *graphicsPrefixResult;

    if(!m_raytracingSystem.prepareCausticOpticalReadTracking(meshViewBufferSnapshot)){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not prepare accepted optical reader tracking"));
        return;
    }
    const b8 causticOpticalReader = declaresHardwareCaustics ? m_raytracingSystem.hasHwCausticWork(meshViewBufferSnapshot) : m_raytracingSystem.hasCausticWork(meshViewBufferSnapshot);

    const Span<const Core::GpuGraphResourceId> causticTraceGeometryResources = declaresHardwareCaustics
        ? Span<const Core::GpuGraphResourceId>(traceGeometry.hardwareTraceGeometry)
        : Span<const Core::GpuGraphResourceId>(traceGeometry.softwareTraceGeometry)
    ;
    const Core::GpuGraphResourceSetId causticTraceGeometrySet = declaresHardwareCaustics ? traceGeometry.hardwareTraceGeometrySet : traceGeometry.softwareTraceGeometrySet;

    // Freeze shadow routing after prefix handoff so transparent folding never sees the prior mask.
    RayTracingShadowVisibilityGraphPlanSnapshot rayTracingShadowVisibilityPlan = m_raytracingSystem.snapshotShadowVisibilityGraphPlan(declaresHardwareCaustics);
    if(rayTracingShadowVisibilityPlan.lightSpace.ready && hasOpaqueCsgFrameWork){
        if(!csgResources.bindingValid() || !frameBindings.meshView.bindingValid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not bind the current direct-shadow receiver cap state"));
            return;
        }
        rayTracingShadowVisibilityPlan.lightSpace.push.csgFlags |= NWB_CSG_SHADOW_FLAG_CURRENT_RECEIVER_CAPS;
        rayTracingShadowVisibilityPlan.lightSpace.receiverView = frameBindings.meshView.buffer;
        rayTracingShadowVisibilityPlan.lightSpace.push.receiverViewSlot = frameBindings.meshView.heapHandle.slot();
        rayTracingShadowVisibilityPlan.lightSpace.push.receiverCsgSampleStateSlot = csgResources.intervalSampleStateHeapHandle.slot();
        rayTracingShadowVisibilityPlan.lightSpace.receiverCsgSampleState = csgResources.intervalSampleState;
    }
    const RayTracingSceneGraphResources sceneResources = m_raytracingSystem.snapshotSceneGraphResources();
    RayTracingSceneGraphReads sceneReads(scratchArena);
    if(rayTracingShadowVisibilityPlan.hardwareTransparentTrace || (declaresHardwareCaustics && causticOpticalReader)){
        auto sceneReadsResult = ImportRayTracingSceneGraphReads(sceneResources, m_raytracingSystem.sceneTlasBackingInitialState(), m_shadowPrepareGraph.m_rayTraceMaterialContextSlotsUploadTask, m_deferredLightingTaskGraph, scratchArena);
        if(!sceneReadsResult){
            sceneReads = RayTracingSceneGraphReads(scratchArena);
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import shared hardware optical scene reads"));
            return;
        }
        sceneReads = Move(*sceneReadsResult);
    }

    Core::GpuGraphResourceId causticOpticalScene;
    if(causticOpticalReader){
        if(declaresHardwareCaustics)
            causticOpticalScene = sceneReads.uses[LengthOf(sceneReads.uses) - 1u].resource;
        else{
            const auto optical = ImportRayTracingOpticalSceneBuffer(sceneResources.opticalScene, m_shadowPrepareGraph.m_rayTraceMaterialContextSlotsUploadTask, m_deferredLightingTaskGraph, scratchArena);
            if(!optical){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import software caustic optical scene reads"));
                return;
            }
            causticOpticalScene = optical->resource;
        }
    }

    if(useLaggedLightingHistory){
        Core::GpuExternalCompletionDesc lightingHistoryReadReadyDesc;
        lightingHistoryReadReadyDesc.setIdentity(Name(s_FrameGraphNameRenderDeferredLightingLaggedHistoryReadReady)).setMarkerLabel("Lagged Lighting History Read Ready").setToken(laggedLightingHistoryReadReadyToken);
        const auto deferredLightingHistoryReadReadyCompletionResult = m_deferredLightingTaskGraph.importExternalCompletion(lightingHistoryReadReadyDesc);
        if(!deferredLightingHistoryReadReadyCompletionResult){
            m_deferredLightingHistoryReadReadyCompletion = {};
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import lagged-lighting history read-ready completion"));
            return;
        }
        m_deferredLightingHistoryReadReadyCompletion = *deferredLightingHistoryReadReadyCompletionResult;
    }

    if(features.laggedLightingHistoryWriterWaitPending){
        Core::GpuExternalCompletionDesc lightingHistoryWriterDrainDesc;
        lightingHistoryWriterDrainDesc.setIdentity(Name(s_FrameGraphNameRenderDeferredLightingLaggedHistoryWriterDrain)).setMarkerLabel("Lagged Lighting History Writer Drain").setToken(laggedLightingHistoryWriterDrainToken);
        const auto deferredLightingHistoryWriterDrainCompletionResult = m_deferredLightingTaskGraph.importExternalCompletion(lightingHistoryWriterDrainDesc);
        if(!deferredLightingHistoryWriterDrainCompletionResult){
            m_deferredLightingHistoryWriterDrainCompletion = {};
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import lagged-lighting history writer-drain completion"));
            return;
        }
        m_deferredLightingHistoryWriterDrainCompletion = *deferredLightingHistoryWriterDrainCompletionResult;
    }

    if(m_raytracingSystem.hasSurfelWork() && rayTracingSurfelResources.countReadbackSubmissionToken.valid()){
        Core::GpuExternalCompletionDesc surfelCounterReadbackCompletionDesc;
        surfelCounterReadbackCompletionDesc.setIdentity(Name(s_FrameGraphNameRenderSurfelGiCounterReadbackComplete)).setMarkerLabel("Surfel Counter Readback Complete").setToken(surfelCounterReadbackCompletionToken);
        const auto deferredSurfelGiCounterReadbackCompletionResult = m_deferredLightingTaskGraph.importExternalCompletion(surfelCounterReadbackCompletionDesc);
        if(!deferredSurfelGiCounterReadbackCompletionResult){
            m_deferredSurfelGiCounterReadbackCompletion = {};
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import surfel counter-readback completion"));
            return;
        }
        m_deferredSurfelGiCounterReadbackCompletion = *deferredSurfelGiCounterReadbackCompletionResult;
    }

    // Effects start from the prefix packet; declare Shadow/Software first for stable queue assignment.
    const ShadowVisibilityGraphContext shadowGraphContext{
        .arena = m_arena,
        .graphics = m_graphics,
        .graph = m_deferredLightingTaskGraph,
        .rayTracing = m_raytracingSystem,
        .shadowComputeState = m_shadowComputePersistentState,
        .shadowReturnState = m_shadowVisibilityReturnState,
        .shadowPrepared = m_shadowPrepareGraph.m_shadowPreparationOutcome.ready,
    };
    if(!m_shadowVisibilityGraph.declareDeferredShadowVisibilityTask(
        shadowGraphContext,
        deferredLightingResources,
        rayTracingShadowResources,
        rayTracingGraphResources,
        rayTracingShadowVisibilityPlan,
        sceneReads,
        declaresHardwareCaustics,
        worldPosition,
        normal,
        depth,
        currentShadowVisibility,
        currentBindlessSlots,
        sceneShading,
        lights,
        materialContextSlots,
        rayTracingShadowVisibilityPlan.hardwareTransparentTrace
            ? Span<const Core::GpuGraphResourceId>(traceGeometry.hardwareTraceGeometry)
            : Span<const Core::GpuGraphResourceId>(traceGeometry.softwareTraceGeometry),
        rayTracingShadowVisibilityPlan.hardwareTransparentTrace ? traceGeometry.hardwareTraceGeometrySet : traceGeometry.softwareTraceGeometrySet,
        traceGeometry.traceMaterialSampledTextureSet,
        m_graphicsPrefixGraph.m_graphicsPrefixTask,
        features.laggedLightingHistoryWriterWaitPending ? m_deferredLightingHistoryWriterDrainCompletion : Core::GpuExternalCompletionId{},
        scratchArena,
        deferredTargets,
        shadowVisibilityTimingTicket,
        shadowVisibilityAsyncTiming,
        shadowVisibilityTiming,
        opaqueSoftResolveTiming,
        transparentSoftResolveTiming,
        shadowVisibilityOpaqueProduced,
        shadowVisibilityTransparentTraceProduced,
        shadowVisibilityOpaqueFrameIndex
    ))
        return;
    const CausticsGraphContext causticsGraphContext{
        .mesh = m_meshSystem,
        .rayTracing = m_raytracingSystem,
        .graph = m_deferredLightingTaskGraph,
        .causticsComputeState = m_causticsComputePersistentState,
        .irradianceReturnState = m_causticIrradianceReturnState,
        .shadowVisibilityTask = m_shadowVisibilityGraph.m_deferredShadowVisibilityTask,
        .shadowPrepared = m_shadowPrepareGraph.m_shadowPreparationOutcome.ready,
    };
    if(!declaresHardwareCaustics && !m_causticsGraph.declareDeferredSoftwareCausticsTask(
        causticsGraphContext,
        declaresHardwareCaustics,
        deferredLightingResources,
        rayTracingGraphResources,
        worldPosition,
        depth,
        currentCausticIrradiance,
        currentBindlessSlots,
        sceneShading,
        lights,
        materialContextSlots,
        causticOpticalScene,
        traceGeometry.softwareTraceGeometry,
        traceGeometry.softwareTraceGeometrySet,
        traceGeometry.traceMaterialSampledTextureSet,
        scratchArena,
        deferredTargets,
        softwareCausticsTimingTicket,
        causticPhotonTiming,
        causticResolveTiming
    ))
        return;
    const Core::GpuTaskId effectsTask = declaresHardwareCaustics ? m_shadowVisibilityGraph.m_deferredShadowVisibilityTask : m_causticsGraph.m_deferredSoftwareCausticsTask;
    // Declare Surfel GI before hardware/AVBOIT to keep effects -> surfel -> suffix order.
    const SurfelGiGraphContext surfelGraphContext{
        .graph = m_deferredLightingTaskGraph,
        .rayTracing = m_raytracingSystem,
        .computeState = m_surfelGiComputePersistentState,
        .counterState = m_surfelGiCounterPersistentState,
        .irradianceReturnState = m_surfelIrradianceReturnState,
        .frameTimingEndTask = m_deferredFrameTimingEndTask,
    };
    if(!m_surfelGiGraph.declareDeferredSurfelGiTask(
        surfelGraphContext,
        deferredLightingResources,
        rayTracingGraphResources,
        rayTracingSurfelResources,
        worldPosition,
        normal,
        currentSurfelIrradiance,
        currentBindlessSlots,
        sceneShading,
        lights,
        materialContextSlots,
        rayTracingGraphResources.surfelUsesHardwareTrace
            ? Span<const Core::GpuGraphResourceId>(traceGeometry.hardwareTraceGeometry)
            : Span<const Core::GpuGraphResourceId>(traceGeometry.softwareTraceGeometry),
        rayTracingGraphResources.surfelUsesHardwareTrace ? traceGeometry.hardwareTraceGeometrySet : traceGeometry.softwareTraceGeometrySet,
        traceGeometry.traceMaterialSampledTextureSet,
        effectsTask,
        useLaggedLightingHistory,
        m_deferredSurfelGiCounterReadbackCompletion,
        scratchArena,
        deferredTargets,
        surfelGiTimingTicket,
        surfelGiAsyncTiming
    ))
        return;

    // Declare Hardware Caustics before Lighting so declaration order owns the live irradiance RAW edge.
    if(declaresHardwareCaustics){
        HardwareCausticsStageBuilder hardwareCausticsStageBuilder(m_deferredLightingTaskGraph, m_raytracingSystem);
        auto hardwareCausticsStageResult = hardwareCausticsStageBuilder.declare(
            HardwareCausticsStageInputs{
                .targets = deferredTargets,
                .lightingResources = deferredLightingResources,
                .meshViewSnapshot = meshViewBufferSnapshot,
                .rayTracingResources = rayTracingGraphResources,
                .features = features,
                .worldPosition = worldPosition,
                .depth = depth,
                .currentCausticIrradiance = currentCausticIrradiance,
                .currentBindlessSlots = currentBindlessSlots,
                .sceneShading = sceneShading,
                .lights = lights,
                .materialContextSlots = materialContextSlots,
                .opticalScene = causticOpticalScene,
                .hardwareTraceGeometrySet = causticTraceGeometrySet,
                .hardwareTraceGeometryResources = causticTraceGeometryResources,
                .traceMaterialSampledTextureSet = traceGeometry.traceMaterialSampledTextureSet,
                .graphicsPrefixTask = m_graphicsPrefixGraph.m_graphicsPrefixTask,
                .shadowPreparationReady = m_shadowPrepareGraph.m_shadowPreparationOutcome.ready,
                .historyWriterDrainCompletion = m_deferredLightingHistoryWriterDrainCompletion,
                .accumulatorPersistentState = m_hardwareCausticAccumulatorPersistentState,
                .declaresHardwareCaustics = declaresHardwareCaustics,
                .producerDispatched = m_causticsGraph.m_deferredCausticProducerDispatched,
                .timingTicket = hardwareCausticsTimingTicket,
                .photonTiming = causticPhotonTiming,
                .resolveTiming = causticResolveTiming,
            },
            scratchArena
        );
        if(!hardwareCausticsStageResult){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare hardware-caustics stage"));
            return;
        }
        m_causticsGraph.m_deferredCausticIrradianceClearTask = hardwareCausticsStageResult->causticIrradianceClearTask;
        m_causticsGraph.m_deferredCausticAccumulatorBootstrapClearTask = hardwareCausticsStageResult->causticAccumulatorBootstrapClearTask;
        m_causticsGraph.m_deferredCausticAccumulatorDecayTask = hardwareCausticsStageResult->causticAccumulatorDecayTask;
        m_causticsGraph.m_deferredCausticPhotonTask = hardwareCausticsStageResult->causticPhotonTask;
        m_causticsGraph.m_deferredCausticGeometryTask = hardwareCausticsStageResult->causticGeometryTask;
        m_causticsGraph.m_deferredCausticResolvePrepareTask = hardwareCausticsStageResult->causticResolvePrepareTask;
        m_causticsGraph.m_deferredCausticResolveWaveletTasks = hardwareCausticsStageResult->causticResolveWaveletTasks;
        m_causticsGraph.m_deferredCausticResolveUpsampleTask = hardwareCausticsStageResult->causticResolveUpsampleTask;
        m_causticsGraph.m_deferredHardwareCausticsTask = hardwareCausticsStageResult->hardwareCausticsTask;
    }

    const b8 opticalCaptureRequested = hasTransparentRenderers && (m_refractionEnabled || m_reflectionSettings.traceMode != ReflectionTraceMode::Disabled);
    const b8 refractionActive = m_preparedRefractionActive && opticalCaptureRequested;
    RayTracingRefractionGraphResources refractionResources = m_preparedRefractionResources;
    FrameGraphAvboitStage avboitStage(m_deferredLightingTaskGraph, m_arena, m_graphics, m_materialSystem, m_csgSystem, m_avboitSystem, m_deferredTaskTimingFeedback);
    const auto avboitStageResult = avboitStage.declare(
        scratchArena,
        FrameGraphAvboitStageInputs{
            .targets = deferredTargets,
            .resources = *deferredGraphResources,
            .csgFrameState = csgFrameState,
            .frameBindings = frameBindings,
            .csgResources = csgResources,
            .meshViewState = meshViewState,
            .prefixTask = m_graphicsPrefixGraph.m_graphicsPrefixTask,
            .hasTransparentRenderers = hasTransparentRenderers,
            .clearTargets = clearAvboitTargets,
            .refractionCaptureEnabled = refractionActive && refractionResources.valid(),
        },
        FrameGraphAvboitStageTiming{
            .preTimingTicket = avboitPreTimingTicket,
            .depthWarpTimingTicket = avboitDepthWarpTimingTicket,
            .extinctionTimingTicket = avboitExtinctionTimingTicket,
            .integrationTimingTicket = avboitIntegrationTimingTicket,
            .accumulationTimingTicket = avboitAccumulationTimingTicket,
            .clearTimingState = avboitClearTimingState,
            .transparentCsgIntervalClearTimingState = transparentCsgIntervalClearTimingState,
            .transparentCsgIntervalsTiming = transparentCsgIntervalsTiming,
            .occupancyComputeEmulationTiming = avboitOccupancyComputeEmulationTiming,
            .extinctionComputeEmulationTiming = avboitExtinctionComputeEmulationTiming,
            .accumulationComputeEmulationTiming = avboitAccumulationComputeEmulationTiming,
        },
        objectGeometry
    );
    if(!avboitStageResult)
        return;
    const Core::GpuTaskId avboitFinalTask = *avboitStageResult;

    DeferredLightingStageBuilder deferredLightingStageBuilder(m_deferredLightingTaskGraph, m_deferredSystem);
    auto deferredLightingStageResult = deferredLightingStageBuilder.declare(
        DeferredLightingStageInputs{
            .targets = deferredTargets,
            .albedo = albedo,
            .normal = normal,
            .worldPosition = worldPosition,
            .depth = depth,
            .shadowVisibility = shadowVisibility,
            .causticIrradiance = causticIrradiance,
            .surfelIrradiance = surfelIrradiance,
            .sceneShading = sceneShading,
            .lights = lights,
            .bindlessSlots = bindlessSlots,
            .opaqueColor = opaqueColor,
            .graphicsPrefixTask = m_graphicsPrefixGraph.m_graphicsPrefixTask,
            .shadowVisibilityTask = m_shadowVisibilityGraph.m_deferredShadowVisibilityTask,
            .surfelGiTask = m_surfelGiGraph.m_deferredSurfelGiTask,
            .avboitFinalTask = avboitFinalTask,
            .hardwareCausticsTask = m_causticsGraph.m_deferredHardwareCausticsTask,
            .softwareCausticsTask = m_causticsGraph.m_deferredSoftwareCausticsTask,
            .historyReadReadyCompletion = m_deferredLightingHistoryReadReadyCompletion,
            .history = history,
            .useLaggedLightingHistory = useLaggedLightingHistory,
            .declaresHardwareCaustics = declaresHardwareCaustics,
            .hasTransparentRenderers = hasTransparentRenderers,
        },
        lightingTimingTicket
    );
    if(!deferredLightingStageResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred-lighting graph task"));
        return;
    }
    m_deferredLaggedLightingHistorySlotsUploadTask = deferredLightingStageResult->historySlotsUploadTask;
    m_deferredLightingTask = deferredLightingStageResult->lightingTask;

    FrameGraphReflectionResolve reflectionResolve(m_deferredLightingTaskGraph, m_graphics, m_raytracingSystem, m_reflectionSystem);
    auto reflectionResolveResult = reflectionResolve.declare(
        FrameGraphReflectionResolveInputs{
            .targets = deferredTargets,
            .meshViewSnapshot = meshViewBufferSnapshot,
            .sceneResources = sceneResources,
            .reflectionSettings = m_reflectionSettings,
            .contentStamp = reflectionContentStamp,
            .csgFrameState = csgFrameState,
            .reflectionFrameIndex = m_reflectionFrameIndex,
            .reflectionSceneAvailable = m_preparedReflectionSceneAvailable,
            .useLaggedLightingHistory = useLaggedLightingHistory,
            .specularRoughness = specularRoughness,
            .refractionSpecularRoughness = refractionSpecularRoughness,
            .normal = normal,
            .depth = depth,
            .worldPosition = worldPosition,
            .refractionDepth = refractionDepth,
            .refractionNormalIor = refractionNormalIor,
            .opaqueColor = opaqueColor,
            .meshView = meshView,
            .currentBindlessSlots = currentBindlessSlots,
            .sceneShading = sceneShading,
            .lights = lights,
            .hardwareTraceGeometrySet = traceGeometry.hardwareTraceGeometrySet,
            .traceMaterialSampledTextureSet = traceGeometry.traceMaterialSampledTextureSet,
            .traceContextUploadTask = m_shadowPrepareGraph.m_rayTraceMaterialContextSlotsUploadTask,
            .lightingTask = m_deferredLightingTask,
            .hardwarePreparationReady = m_shadowPrepareGraph.m_shadowPreparationOutcome.ready,
            .hardwareDispatchLogged = m_reflectionHardwareLogged,
            .fallbackDispatchLogged = m_reflectionFallbackLogged,
        },
        scratchArena,
        sceneReads
    );
    if(!reflectionResolveResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare reflection graph tasks"));
        return;
    }
    const ReflectionGraphResult reflectionGraph = reflectionResolveResult->reflectionGraph;
    const ReflectionCompositeInputs reflectionCompositeInputs = reflectionResolveResult->reflectionCompositeInputs;
    refractionResources.opaqueReflectionSlot = reflectionCompositeInputs.opaqueRadianceSlot;

    FrameGraphRefractionResolve refractionResolveBuilder(m_deferredLightingTaskGraph, m_graphics, m_raytracingSystem);
    auto refractionResolveResult = refractionResolveBuilder.declare(
        FrameGraphRefractionResolveInputs{
            .targets = deferredTargets,
            .sceneResources = sceneResources,
            .refractionResources = refractionResources,
            .refractionActive = refractionActive,
            .depth = depth,
            .worldPosition = worldPosition,
            .refractionDepth = refractionDepth,
            .refractionNormalIor = refractionNormalIor,
            .refractionTintCoverage = refractionTintCoverage,
            .refractionInstance = refractionInstance,
            .refractionResolve = refractionResolve,
            .opaqueColor = opaqueColor,
            .opaqueReflectionRadiance = reflectionGraph.opaqueRadiance,
            .meshView = meshView,
            .currentBindlessSlots = currentBindlessSlots,
            .sceneShading = sceneShading,
            .lights = lights,
            .avboitAccumColor = avboitAccumColor,
            .avboitAccumExtinction = avboitAccumExtinction,
            .avboitForegroundColor = avboitForegroundColor,
            .avboitForegroundExtinction = avboitForegroundExtinction,
            .hardwareTraceGeometrySet = traceGeometry.hardwareTraceGeometrySet,
            .traceMaterialSampledTextureSet = traceGeometry.traceMaterialSampledTextureSet,
            .traceContextUploadTask = m_shadowPrepareGraph.m_rayTraceMaterialContextSlotsUploadTask,
            .reflectionCompletionTask = reflectionGraph.completion,
            .avboitFinalTask = avboitFinalTask,
            .surfelGiTask = m_surfelGiGraph.m_deferredSurfelGiTask,
            .hardwarePreparationReady = m_shadowPrepareGraph.m_shadowPreparationOutcome.ready,
            .refractionHardwareLogged = m_refractionHardwareLogged,
            .refractionScreenLogged = m_refractionScreenLogged,
        },
        scratchArena,
        sceneReads
    );
    if(!refractionResolveResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare refraction resolve graph tasks"));
        return;
    }
    const Core::GpuTaskId refractionResolveTask = *refractionResolveResult;

    DeferredGraphSuffixBuilder suffixBuilder(m_deferredLightingTaskGraph, m_deferredSystem, m_graphics, m_preparedTaskGraphPresentationContributor, m_preparedTaskGraphOutputLayerContributor);
    auto suffixResult = suffixBuilder.declare(
        DeferredGraphSuffixInputs{
            .targets = deferredTargets,
            .opaqueColor = opaqueColor,
            .avboitAccumColor = avboitAccumColor,
            .avboitAccumExtinction = avboitAccumExtinction,
            .avboitForegroundColor = avboitForegroundColor,
            .avboitForegroundExtinction = avboitForegroundExtinction,
            .refractionResolve = refractionResolve,
            .refractionDepth = refractionDepth,
            .currentBindlessSlots = currentBindlessSlots,
            .reflectionGraph = reflectionGraph,
            .reflectionCompositeInputs = reflectionCompositeInputs,
            .useLaggedLightingHistory = useLaggedLightingHistory,
            .lightingTask = m_deferredLightingTask,
            .avboitFinalTask = avboitFinalTask,
            .refractionResolveTask = refractionResolveTask,
            .surfelGiTask = m_surfelGiGraph.m_deferredSurfelGiTask,
            .hardwareCausticsTask = declaresHardwareCaustics ? m_causticsGraph.m_deferredHardwareCausticsTask : Core::GpuTaskId{},
            .presentationFrame = presentationFrame,
            .presentationFramebufferDesc = presentationFramebufferDesc,
        },
        m_shadowVisibilityGraph.m_deferredShadowVisibilityTask,
        compositeTimingTicket,
        presentTimingTicket,
        asyncFinalTiming,
        frameTimingTransaction
    );
    if(!suffixResult)
        return;
    m_deferredCompositeTask = suffixResult->compositeTask;
    m_deferredPresentationOverlayRequired = suffixResult->overlayRequired;
    m_deferredPresentationOverlayTask = suffixResult->overlayTask;
    m_deferredPresentTask = suffixResult->presentTask;
    m_deferredFrameTimingEndTask = suffixResult->frameTimingEndTask;

    m_surfelGiGraph.declareDeferredSurfelCountReadbackTask(surfelGraphContext, rayTracingSurfelResources);

    DeferredFrameTailBuilder deferredFrameTailBuilder(m_deferredLightingTaskGraph);
    auto deferredFrameTailResult = deferredFrameTailBuilder.declare(
        DeferredFrameTailInputs{
            .frameTimingTransaction = frameTimingTransaction,
            .historyCopySubmissionToken = m_laggedLightingHistorySubmissionToken,
            .recoveryArmed = m_deferredFrameRecoveryArmed,
            .recoveryRetiresFrameTiming = m_deferredFrameRecoveryRetiresTiming,
            .terminalPresentationTask = m_deferredFrameTimingEndTask,
            .historyCopyShadowVisibility = historyCopyShadowVisibility,
            .historyCopyCausticIrradiance = historyCopyCausticIrradiance,
            .historyCopySurfelIrradiance = historyCopySurfelIrradiance,
            .historyCopyDestinationShadowVisibility = historyCopyDestinationShadowVisibility,
            .historyCopyDestinationCausticIrradiance = historyCopyDestinationCausticIrradiance,
            .historyCopyDestinationSurfelIrradiance = historyCopyDestinationSurfelIrradiance,
            .capturesLaggedLightingHistory = capturesLaggedLightingHistory,
        });
    if(!deferredFrameTailResult)
        return;
    m_deferredLaggedLightingHistoryTask = deferredFrameTailResult->historyCopyTask;
    m_deferredFrameRecoveryTask = deferredFrameTailResult->recoveryTask;

    m_deferredLightingTaskGraphDeclarationSeconds = DurationInSeconds<f64>(TimerNow(), declarationBegin);
    m_deferredLightingTaskGraphDeclared = true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererFramePipeline::prepareDeferredGraphPacketEnvelopeMetrics(const Core::GpuTaskGraph::DeclarationReadView& graph, const Core::GpuCompiledGraph::ReadView& compiledGraph, Core::Alloc::ScratchArena& scratchArena){
    const Core::GpuSubmissionPacketRange range = compiledGraph.packetTimingEnvelopeRange();
    if(!range.valid() || !compiledGraph.validPacketRange(range))
        return false;

    Vector<Core::GpuPacketEnvelopeMetricScope, Core::Alloc::ScratchArena> packetScopes{scratchArena};
    Vector<Core::GpuPacketEnvelopeMetricQueueOutput, Core::Alloc::ScratchArena> queueOutputs{scratchArena};
    packetScopes.reserve(range.packetCount);
    queueOutputs.reserve(range.packetCount);
    for(usize packetOffset = 0u; packetOffset < range.packetCount; ++packetOffset){
        const Core::GpuSubmissionPacketId packetID = compiledGraph.packetIdAt(range.first.index + packetOffset);
        const Core::GpuCompiledPacketView packetView = compiledGraph.packet(packetID);
        if(!packetView.valid() || !packetView.plan->recordsPacketEnvelopeTiming || packetView.plan->taskCount == 0u)
            return false;

        const auto packetScopeName = Core::GpuTaskPacketTimingScopeName(graph.taskAt(packetView.tasks[0u].index).identity);
        if(!packetScopeName)
            return false;
        packetScopes.push_back(Core::GpuPacketEnvelopeMetricScope{ .scopeName = *packetScopeName, .physicalQueue = packetView.plan->queue, });

        b8 hasQueueOutput = false;
        for(const Core::GpuPacketEnvelopeMetricQueueOutput& output : queueOutputs)
            hasQueueOutput = hasQueueOutput || output.physicalQueue == packetView.plan->queue;
        if(hasQueueOutput)
            continue;

        const auto internalIdleScopeName = RendererGpuTimingScope::DeferredGraphQueueInternalIdle(packetView.plan->queue, scratchArena);
        if(!internalIdleScopeName || !*internalIdleScopeName)
            return false;
        queueOutputs.push_back(Core::GpuPacketEnvelopeMetricQueueOutput{ .physicalQueue = packetView.plan->queue, .internalIdleScopeName = **internalIdleScopeName, });
    }

    return m_graphics.gpuTiming().preparePacketEnvelopeMetrics(
        m_graphics.getFrameIndex(),
        packetScopes,
        RendererGpuTimingScope::s_DeferredGraphQueueOverlap.identity,
        queueOutputs
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererFramePipeline::declareFrameTimingBeginTask(Core::GpuTimingFrameTransaction& frameTimingTransaction){
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Tiny;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    const auto deferredFrameTimingBeginTaskResult = m_deferredLightingTaskGraph.addTask<Core::FrameTimingBeginGraphTask>(
        Core::GpuTaskDesc{}
            .setIdentity(Name(s_FrameGraphNameRenderFrameTimingBegin))
            .setMarkerLabel("Frame Timing Begin")
            .setScheduling(scheduling),
        Core::FrameTimingBeginGraphTask::Payload{ .frameTimingTransaction = frameTimingTransaction, .device = m_graphics.getDevice(), .scopeDefinition = RendererGpuTimingScope::s_Frame, });
    if(!deferredFrameTimingBeginTaskResult || !m_deferredLightingTaskGraph.setNormalExecutionPrelude(*deferredFrameTimingBeginTaskResult)){
        if(deferredFrameTimingBeginTaskResult)
            m_deferredFrameTimingBeginTask = *deferredFrameTimingBeginTaskResult;
        else
            m_deferredFrameTimingBeginTask = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare frame timing prelude"));
        return false;
    }
    m_deferredFrameTimingBeginTask = *deferredFrameTimingBeginTaskResult;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererFramePipeline::scheduleDeferredLightingTaskGraphForExecution(Core::Alloc::ScratchArena& scratchArena){
    m_deferredLightingTaskGraphScheduled = false;
    if(!m_deferredLightingTaskGraphDeclared)
        return false;

    Core::GpuTaskGraphCompileOptions compileOptions;
    // A graphics prefix can split immediately after work that enables a different physical queue.
    // This exposes the true cross-queue frontier while preserving the compiler's declaration-derived dependency order.
    compileOptions.packetizationPolicy = Core::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    // Time accepted packets through the presentation endpoint. Readback, history-copy, and recovery tails keep their own policy.
    compileOptions.packetTimingEnvelope.firstTask = m_deferredFrameTimingBeginTask;
    compileOptions.packetTimingEnvelope.lastTask = m_deferredFrameTimingEndTask;
    m_deferredTaskTimingFeedback.configureCompileOptions(compileOptions, m_graphics.getFrameIndex());
    compileOptions.declarationSeconds = m_deferredLightingTaskGraphDeclarationSeconds;

    const Core::GpuTaskScheduler& scheduler = m_graphics.gpuTasks();
    if(!scheduler.scheduleGraph(
        m_deferredLightingTaskGraph,
        compileOptions,
        scratchArena,
        m_deferredLightingTaskGraphAnalysis,
        m_deferredLightingTaskGraphQueueAssignments,
        m_deferredLightingCompiledGraph,
        m_deferredLightingRecordedGraph,
        m_deferredLightingSubmissionTransaction
    )){
        const auto& analysisDiagnostic = m_deferredLightingTaskGraphAnalysis.diagnostic();
        const auto& queueDiagnostic = m_deferredLightingTaskGraphQueueAssignments.diagnostic();
        DeferredFrameDiagnostics{}.logDeferredGraphSchedulingFailure(
            static_cast<u32>(analysisDiagnostic.status),
            analysisDiagnostic.task.index,
            analysisDiagnostic.resource.index,
            static_cast<u32>(queueDiagnostic.status),
            queueDiagnostic.task.index
        );
        return false;
    }

    const Core::GpuTaskGraph::DeclarationReadView declarations(m_deferredLightingTaskGraph);
    const Core::GpuCompiledGraph::ReadView compiledPlan(m_deferredLightingCompiledGraph);
    if(!compiledPlan.validFor(declarations)){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: deferred graph scheduling lost its compiler plan"));
        return false;
    }
    if(!prepareDeferredGraphPacketEnvelopeMetrics(declarations, compiledPlan, scratchArena))
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not prepare deferred graph packet metrics"));

    m_deferredLightingTaskGraphScheduled = true;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<u64> GraphicsPrefixGraphCoordinator::declareDeferredGraphicsPrefixTasks(
    const GraphicsPrefixGraphContext& context,
    const Core::GpuTaskId shadowPrepareTask,
    const CsgFrameState& csgFrameState,
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings,
    const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources,
    const b8 hasOpaqueCsgFrameWork,
    const f32 meshViewAspectRatio,
    const MeshViewGpuData& meshViewState,
    const b8 meshViewUploadRequired,
    const DeferredGraphResourceImportResult& resources,
    const Span<const Core::GpuGraphResourceId> shadowTraceGeometryResources,
    const Core::GpuGraphResourceSetId shadowTraceGeometrySet,
    const b8& asyncPrefixTimingSpansOnePacket,
    Core::Alloc::ScratchArena& scratchArena,
    DeferredFrameTargets& deferredTargets,
    ObjectGeometryCacheGraph& objectGeometry,
    Optional<Core::GpuTimingMeasure>& asyncPrefixTiming,
    Optional<Core::GpuTimingMeasure>& deferredClearTiming,
    GraphClearTimingRecordState& deferredClearTimingState,
    GraphClearTimingRecordState& csgIntervalClearTimingState,
    Optional<Core::GpuTimingMeasure>& opaqueRegularSharedComputeEmulationTiming,
    Optional<Core::GpuTimingMeasure>& opaqueCsgIntervalSampleComputeEmulationTiming,
    const Span<Core::GpuTimingSubmissionTicket*> timingTickets
){
    using namespace RendererTaskGraphDetail;

    m_graphicsPrefixMeshViewSetupTask = {};
    m_graphicsPrefixSceneShadingSetupTask = {};
    m_graphicsPrefixDeferredClearTask = {};
    m_graphicsPrefixCsgIntervalClearFirstTask = {};
    m_graphicsPrefixCsgIntervalClearTask = {};
    resetGraphicsPrefixTaskState();
    m_graphicsPrefixMeshViewSetupReady = false;
    m_graphicsPrefixSceneShadingSetupReady = false;

    const DeferredLightingGraphResources deferredLightingResources = context.deferred.lightingGraphResources();

    if(
        !deferredTargets.valid()
        || !shadowPrepareTask.valid()
        || !deferredLightingResources.valid()
        || !resources.albedo.valid()
        || !resources.normal.valid()
        || !resources.worldPosition.valid()
        || !resources.specularRoughness.valid()
        || !resources.depth.valid()
        || !resources.opaqueColor.valid()
        || !resources.sceneShading.valid()
        || !resources.lights.valid()
        || !resources.meshView.valid()
        || !resources.currentBindlessSlots.valid()
        || !resources.materialContextSlots.valid()
        || (hasOpaqueCsgFrameWork && (
            !resources.csgCapBackNormal.valid()
            || !resources.csgIntervalDepth.valid()
            || !resources.csgIntervalId.valid()
            || !resources.csgReceiverEventData.valid()
            || !resources.csgReceiverEventCount.valid()
            || !resources.csgReceiverSpanData.valid()
            || !resources.csgReceiverSpanCount.valid()
            || !resources.csgRemovedIntervalDepth.valid()
            || !resources.csgRemovedIntervalCapNormal.valid()
            || !resources.csgRemovedIntervalData.valid()
            || !resources.csgRemovedIntervalCount.valid()
        ))
        || timingTickets.size() < static_cast<usize>(DeferredGraphicsPrefixTimingSlot::kCount)
        || !deferredClearTimingState.graphics
        || deferredClearTimingState.timing != &deferredClearTiming
        || !deferredClearTimingState.rebindableTimingTicket
    )
        return MakeUnexpected(Failure{});
    for(usize timingSlot = 0u; timingSlot < static_cast<usize>(DeferredGraphicsPrefixTimingSlot::kCount); ++timingSlot){
        if(!timingTickets[timingSlot])
            return MakeUnexpected(Failure{});
    }
    const auto timingTicketSlot = [timingTickets](const DeferredGraphicsPrefixTimingSlot::Enum slot)noexcept{
        return &timingTickets[static_cast<usize>(slot)];
    };
    const Core::TextureSubresourceSet csgPeelSubresources(0u, 1u, 0u, deferredTargets.csgPeelLayerCount);
    const Core::TextureSubresourceSet csgReceiverEventDataSubresources(0u, 1u, 0u, deferredTargets.csgReceiverEventLayerCount);
    const Core::TextureSubresourceSet csgReceiverEventCountSubresources(0u, 1u, 0u, 1u);
    const Core::TextureSubresourceSet csgRemovedIntervalSubresources(0u, 1u, 0u, deferredTargets.csgRemovedIntervalLayerCount);
    const Core::TextureSubresourceSet csgRemovedIntervalCountSubresources(0u, 1u, 0u, 1u);

    const RayTracingLightingClassificationInput rayTracingLightingInput = context.rayTracing.snapshotLightingClassificationInput();
    PrefixSceneUploadBuilder prefixSceneUploadBuilder(context.graph, context.deferred, context.mesh, context.graphics);
    auto prefixSceneUploadResult = prefixSceneUploadBuilder.declare(
        PrefixSceneUploadInputs{
            .rayTracingLightingInput = rayTracingLightingInput,
            .meshViewState = meshViewState,
            .meshView = resources.meshView,
            .lights = resources.lights,
            .sceneShading = resources.sceneShading,
            .shadowPrepareTask = shadowPrepareTask,
            .meshViewAspectRatio = meshViewAspectRatio,
            .meshViewUploadRequired = meshViewUploadRequired,
            .asyncPrefixTiming = asyncPrefixTiming,
            .meshViewSetupTimingTicket = *timingTicketSlot(DeferredGraphicsPrefixTimingSlot::MeshViewSetup),
            .sceneShadingSetupTimingTicket = *timingTicketSlot(DeferredGraphicsPrefixTimingSlot::SceneShadingSetup),
            .asyncPrefixTimingSpansOnePacket = asyncPrefixTimingSpansOnePacket,
            .shadowVisibilityTask = context.shadowVisibilityTask,
            .meshViewSetupReady = m_graphicsPrefixMeshViewSetupReady,
            .sceneShadingSetupReady = m_graphicsPrefixSceneShadingSetupReady,
        });
    if(!prefixSceneUploadResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graphics-prefix scene uploads"));
        return MakeUnexpected(Failure{});
    }
    m_graphicsPrefixMeshViewSetupTask = prefixSceneUploadResult->meshViewSetupTask;
    m_graphicsPrefixSceneShadingSetupTask = prefixSceneUploadResult->sceneShadingSetupTask;
    const RayTracingLightingClassification rayTracingLightingClassification = prefixSceneUploadResult->lightingClassification;
    const u32 sceneLightCount = prefixSceneUploadResult->lightCount;
    // G-buffer load clears stay in attachment storage and G-buffer timing.
    // Deferred-clear timing covers the opaque-color UAV clear before its Compute handoff.
    Core::GpuTaskSchedulingHint clearScheduling;
    clearScheduling.cost = Core::GpuTaskCostHint::Tiny;
    clearScheduling.forceSubmissionBoundary = false;
    clearScheduling.allowPacketMerge = true;
    clearScheduling.mergeWithPrevious = true;
    const Core::GpuClearTextureTaskRecordHooks deferredClearHooks{ .context = &deferredClearTimingState, .beforeClear = &BeginGraphClearTimingRecord, .afterClear = &EndGraphClearTimingRecord, .discarded = &DiscardGraphClearTimingRecord, };
    Core::GpuTaskDesc deferredClearDesc;
    deferredClearDesc.setIdentity(Name(s_FrameGraphNameRenderGraphicsPrefixDeferredClearOpaqueColor)).setMarkerLabel("Deferred Clear Opaque Color").setScheduling(clearScheduling).setDependencies({ &m_graphicsPrefixSceneShadingSetupTask, 1u });
    Core::GpuClearTextureTaskDesc opaqueColorClearDesc;
    opaqueColorClearDesc.destination = resources.opaqueColor;
    opaqueColorClearDesc.subresources = ECSRenderDetail::s_FramebufferSubresources;
    opaqueColorClearDesc.valueType = Core::GpuClearTextureTaskValueType::Float;
    opaqueColorClearDesc.floatValue = ECSRenderDetail::s_ClearColor;
    opaqueColorClearDesc.recordHooks = deferredClearHooks;
    const auto graphicsPrefixDeferredClearTaskResult = context.graph.addClearTextureTask(deferredClearDesc, opaqueColorClearDesc);
    if(!graphicsPrefixDeferredClearTaskResult){
        m_graphicsPrefixDeferredClearTask = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred-clear task"));
        return MakeUnexpected(Failure{});
    }
    m_graphicsPrefixDeferredClearTask = *graphicsPrefixDeferredClearTaskResult;

    // Freeze the opaque packet's draw ordering before recording.
    // The two copied blobs below are the exact source of the heap-selected instance/material streams consumed by these saved draw items.
    MaterialPassDrawItemPartitions opaqueDrawItems{ scratchArena };
    InstanceGpuDataVector instanceData{ scratchArena };
    CsgFrameGpuData csgFrameData{ scratchArena };
#if defined(NWB_DEBUG)
    ECSRenderDetail::MaterialTypedInstanceRangeVector materialTypedRanges{ scratchArena };
#endif
    MaterialTypedByteDataVector materialTypedBytes{ scratchArena };
    context.material.gatherMaterialPassDrawItems(
        deferredTargets.framebuffer.get(),
        MaterialPipelinePass::Opaque,
        false,
        csgFrameState,
        RendererResourceLookupMode::PreparedOnly,
        &meshViewState,
        opaqueDrawItems,
        instanceData,
        csgFrameData,
#if defined(NWB_DEBUG)
        materialTypedRanges,
#endif
        materialTypedBytes
    );

    ECSRenderDetail::GbufferGraphTask::Payload gbufferPayload{
        context.arena,
        context.graphics,
        context.material,
        context.csg,
        deferredTargets,
        *timingTicketSlot(DeferredGraphicsPrefixTimingSlot::Gbuffer),
        m_graphicsPrefixMeshViewSetupReady,
        m_graphicsPrefixSceneShadingSetupReady,
    };
    gbufferPayload.frameBindings = frameBindings;
    gbufferPayload.csgResources = csgResources;

    const b8 hasOpaqueDrawItems = !opaqueDrawItems.empty();
    // G-buffer and the opaque CSG follow-up declare the shared material entry batch. The source-geometry batch is retained below.
    gbufferPayload.materialFrameStatesGraphOwned = hasOpaqueDrawItems;
    OpaqueUploadChainBuilder opaqueUploadChainBuilder(context.graph, context.material, context.csg);
    auto opaqueUploadChainResult = opaqueUploadChainBuilder.declare(
        OpaqueUploadChainInputs{
            .targets = deferredTargets,
            .frameBindings = frameBindings,
            .csgResources = csgResources,
            .drawItems = opaqueDrawItems,
            .instanceData = instanceData,
            .csgFrameData = csgFrameData,
#if defined(NWB_DEBUG)
            .materialTypedRanges = materialTypedRanges,
#endif
            .materialTypedBytes = materialTypedBytes,
            .materialInstances = resources.materialInstances,
            .materialTyped = resources.materialTyped,
            .csgReceiverRanges = resources.csgReceiverRanges,
            .csgCutters = resources.csgCutters,
            .csgClipContextSlots = resources.csgClipContextSlots,
            .csgIntervalSampleState = resources.csgIntervalSampleState,
            .dependencyTask = m_graphicsPrefixDeferredClearTask,
        });
    if(!opaqueUploadChainResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare opaque upload chain"));
        return MakeUnexpected(Failure{});
    }
    const Core::GpuTaskId csgFrameUploadTask = opaqueUploadChainResult->csgUploadTask;
    const b8 hasCsgFrameGpuWork = opaqueUploadChainResult->hasCsgFrameGpuWork;
    gbufferPayload.materialDrawBuffersUploaded = opaqueUploadChainResult->materialDrawBuffersUploaded;
    gbufferPayload.csgFrameBuffersUploaded = opaqueUploadChainResult->csgFrameBuffersUploaded;

    gbufferPayload.opaqueDrawSnapshot.capture(opaqueDrawItems, csgFrameData, instanceData.size(), materialTypedBytes.size());
    OpaqueCsgIntervalStage opaqueCsgIntervalStage(
        context.graph,
        context.arena,
        context.graphics,
        context.material,
        context.csg,
        deferredTargets,
        *timingTicketSlot(DeferredGraphicsPrefixTimingSlot::CsgReceiverSpanBuild),
        *timingTicketSlot(DeferredGraphicsPrefixTimingSlot::CsgIntervalCombine),
        *timingTicketSlot(DeferredGraphicsPrefixTimingSlot::CsgIntervalSample),
        m_graphicsPrefixMeshViewSetupReady,
        m_graphicsPrefixSceneShadingSetupReady,
        opaqueCsgIntervalSampleComputeEmulationTiming
    );
    opaqueCsgIntervalStage.capturePayloads(
        frameBindings,
        csgResources,
        opaqueDrawItems,
        csgFrameData,
        instanceData.size(),
        materialTypedBytes.size(),
        gbufferPayload.materialDrawBuffersUploaded,
        gbufferPayload.csgFrameBuffersUploaded,
        hasOpaqueDrawItems,
        hasOpaqueCsgFrameWork
    );

    // G-buffer makes peel/event images; span build publishes spans; Combine publishes removed-interval images; Sample takes them.
    // Their native thunks consume graph-owned StorageImage state without staging target transitions.

    // The semantic opaque-CSG flag admits this clear before native readiness checks.
    // The graph owns both CopyDest writes and their following UAV handoff.
    OpaqueCsgIntervalClearBuilder opaqueCsgIntervalClearBuilder(context.graph);
    auto opaqueCsgIntervalClearResult = opaqueCsgIntervalClearBuilder.declare(
        OpaqueCsgIntervalClearInputs{
            .targets = deferredTargets,
            .csgFrameData = csgFrameData,
            .csgIntervalId = resources.csgIntervalId,
            .csgReceiverEventCount = resources.csgReceiverEventCount,
            .csgPeelSubresources = csgPeelSubresources,
            .csgReceiverEventCountSubresources = csgReceiverEventCountSubresources,
            .dependencyTask = csgFrameUploadTask,
            .hasOpaqueCsgFrameWork = hasOpaqueCsgFrameWork,
        },
        csgIntervalClearTimingState
    );
    if(!opaqueCsgIntervalClearResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare opaque CSG interval clears"));
        return MakeUnexpected(Failure{});
    }
    m_graphicsPrefixCsgIntervalClearFirstTask = opaqueCsgIntervalClearResult->clearFirstTask;
    m_graphicsPrefixCsgIntervalClearTask = opaqueCsgIntervalClearResult->clearTask;
    const Core::GpuTaskId csgIntervalClearTask = hasOpaqueCsgFrameWork ? opaqueCsgIntervalClearResult->clearTask : csgFrameUploadTask;

    // Capacity grows independently of each frozen draw stream. Synchronize only the bytes uploaded for this pass.
    const Core::BufferRange materialInstanceRange(0u, instanceData.size() * sizeof(InstanceGpuData));
    const Core::BufferRange materialTypedRange(0u, materialTypedBytes.size());
    const Core::BufferRange csgReceiverRange(0u, csgFrameData.receiverRanges.size() * sizeof(CsgReceiverRangeGpuData));
    const Core::BufferRange csgCutterRange(0u, csgFrameData.cutters.size() * sizeof(CsgCutterGpuData));

    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> gbufferResourceUses{ scratchArena };
    OpaqueGbufferMaterialResourceCollector gbufferMaterialResourceCollector(context.graph, context.material, scratchArena);
    const auto gbufferMaterialResources = gbufferMaterialResourceCollector.gather(opaqueDrawItems);
    if(!gbufferMaterialResources)
        return MakeUnexpected(Failure{});
    gbufferPayload.materialGeometryStatesGraphOwned = gbufferMaterialResources->geometryStatesGraphOwned;
    const Core::GpuGraphResourceSetId gbufferMaterialGeometrySet = gbufferMaterialResources->geometrySet;
    const Core::GpuGraphResourceSetId gbufferMaterialSampledTextureSet = gbufferMaterialResources->sampledTextureSet;
    const b8 gbufferMaterialSampledTexturesCollected = gbufferMaterialResources->sampledTexturesCollected;

    OpaqueComputeEmulationPlanner opaqueComputeEmulationPlanner(
        context.arena,
        context.material,
        deferredTargets,
        *timingTicketSlot(DeferredGraphicsPrefixTimingSlot::Gbuffer),
        m_graphicsPrefixMeshViewSetupReady,
        m_graphicsPrefixSceneShadingSetupReady
    );
    opaqueComputeEmulationPlanner.capture(
        context.graph,
        scratchArena,
        opaqueDrawItems,
        csgFrameData,
        frameBindings,
        hasOpaqueCsgFrameWork,
        hasCsgFrameGpuWork,
        gbufferMaterialSampledTexturesCollected,
        gbufferPayload,
        opaqueRegularSharedComputeEmulationTiming
    );
    auto& opaqueComputeEmulationPayload = opaqueComputeEmulationPlanner.regularPayload();
    auto& opaqueCsgReceiverComputeEmulationPayload = opaqueComputeEmulationPlanner.csgPayload();
    const auto& opaqueSharedComputeEmulationPlan = opaqueComputeEmulationPlanner.sharedPlan();
    const Core::GpuGraphResourceSetId opaqueComputeEmulationOutputSet = opaqueComputeEmulationPlanner.regularOutputSet();
    const Core::GpuGraphResourceSetId opaqueCsgReceiverComputeEmulationOutputSet = opaqueComputeEmulationPlanner.csgOutputSet();
    const Core::GpuGraphResourceId opaqueSharedComputeEmulationOutput = opaqueComputeEmulationPlanner.sharedOutput();
    const b8 opaqueComputeEmulationOutputStatesGraphOwned = opaqueComputeEmulationPlanner.regularOutputOwned();
    const b8 opaqueSharedComputeEmulationOutputStatesGraphOwned = opaqueComputeEmulationPlanner.sharedOutputOwned();
    const b8 opaqueCsgReceiverComputeEmulationOutputStatesGraphOwned = opaqueComputeEmulationPlanner.csgOutputOwned();

    OpaqueGbufferResourceUseBuilder::Append(
        OpaqueGbufferResourceUseInputs{
            .resources = resources,
            .materialInstanceRange = materialInstanceRange,
            .materialTypedRange = materialTypedRange,
            .csgReceiverRange = csgReceiverRange,
            .csgCutterRange = csgCutterRange,
            .csgPeelSubresources = csgPeelSubresources,
            .csgReceiverEventDataSubresources = csgReceiverEventDataSubresources,
            .csgReceiverEventCountSubresources = csgReceiverEventCountSubresources,
            .hasOpaqueDrawItems = hasOpaqueDrawItems,
            .hasCsgFrameGpuWork = hasCsgFrameGpuWork,
            .hasOpaqueCsgFrameWork = hasOpaqueCsgFrameWork,
        },
        gbufferResourceUses
    );
    const Core::GpuTaskResourceSetUse gbufferMaterialGeometrySetUse{ .resourceSet = gbufferMaterialGeometrySet, .range = {}, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read, };
    const Core::GpuTaskResourceSetUse gbufferMaterialSampledTextureSetUse{ .resourceSet = gbufferMaterialSampledTextureSet, .range = {}, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read, };
    const Core::GpuTaskResourceSetUse opaqueComputeEmulationOutputUavSetUse{ .resourceSet = opaqueComputeEmulationOutputSet, .range = {}, .requiredState = Core::ResourceStates::UnorderedAccess, .access = Core::GpuTaskResourceAccess::Write, };
    const Core::GpuTaskResourceSetUse opaqueComputeEmulationOutputVertexBufferSetUse{ .resourceSet = opaqueComputeEmulationOutputSet, .range = {}, .requiredState = ECSRenderDetail::s_ObjectGeometryRasterState, .access = Core::GpuTaskResourceAccess::Read, };
    const Core::GpuTaskResourceSetUse opaqueCsgReceiverComputeEmulationOutputUavSetUse{
        .resourceSet = opaqueCsgReceiverComputeEmulationOutputSet,
        .range = {},
        .requiredState = Core::ResourceStates::UnorderedAccess,
        .access = Core::GpuTaskResourceAccess::Write,
    };
    const Core::GpuTaskResourceSetUse opaqueCsgReceiverComputeEmulationOutputVertexBufferSetUse{
        .resourceSet = opaqueCsgReceiverComputeEmulationOutputSet,
        .range = {},
        .requiredState = ECSRenderDetail::s_ObjectGeometryRasterState,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    Array<Core::GpuTaskResourceSetUse, s_GbufferMaterialResourceSetUseCapacity> gbufferMaterialResourceSetUses = {};
    usize gbufferMaterialResourceSetUseCount = 0u;
    if(gbufferPayload.materialGeometryStatesGraphOwned)
        gbufferMaterialResourceSetUses[gbufferMaterialResourceSetUseCount++] = gbufferMaterialGeometrySetUse;
    if(gbufferMaterialSampledTextureSet.valid())
        gbufferMaterialResourceSetUses[gbufferMaterialResourceSetUseCount++] = gbufferMaterialSampledTextureSetUse;
    if(opaqueComputeEmulationOutputStatesGraphOwned)
        gbufferMaterialResourceSetUses[gbufferMaterialResourceSetUseCount++] = opaqueComputeEmulationOutputVertexBufferSetUse;
    if(opaqueCsgReceiverComputeEmulationOutputStatesGraphOwned)
        gbufferMaterialResourceSetUses[gbufferMaterialResourceSetUseCount++] = opaqueCsgReceiverComputeEmulationOutputVertexBufferSetUse;

    Core::GpuTaskId gbufferDependency = csgIntervalClearTask;
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> objectGeometryReads{scratchArena};
    if(
        gbufferPayload.materialDrawBuffersUploaded
        && !objectGeometry.prepare(opaqueDrawItems.regular.indexedDrawItems, frameBindings, scratchArena, gbufferDependency, objectGeometryReads, nullptr, timingTicketSlot(DeferredGraphicsPrefixTimingSlot::Gbuffer))
    )
        return MakeUnexpected(Failure{});
    if(
        gbufferPayload.materialDrawBuffersUploaded
        && !objectGeometry.prepare(opaqueDrawItems.csgReceiverSurface.indexedDrawItems, frameBindings, scratchArena, gbufferDependency, objectGeometryReads, nullptr, timingTicketSlot(DeferredGraphicsPrefixTimingSlot::Gbuffer))
    )
        return MakeUnexpected(Failure{});
    gbufferResourceUses.insert(gbufferResourceUses.end(), objectGeometryReads.begin(), objectGeometryReads.end());

    const OpaqueComputeEmulationProducerInputs computeEmulationProducerInputs{
        .resources = resources,
        .materialGeometrySetUse = gbufferMaterialGeometrySetUse,
        .materialSampledTextureSetUse = gbufferMaterialSampledTextureSetUse,
        .materialInstanceRange = materialInstanceRange,
        .materialTypedRange = materialTypedRange,
        .csgReceiverRange = csgReceiverRange,
        .csgCutterRange = csgCutterRange,
        .instanceCount = instanceData.size(),
        .materialTypedByteCount = materialTypedBytes.size(),
        .materialDrawBuffersUploaded = gbufferPayload.materialDrawBuffersUploaded,
        .csgFrameBuffersUploaded = gbufferPayload.csgFrameBuffersUploaded,
        .materialFrameStatesGraphOwned = gbufferPayload.materialFrameStatesGraphOwned,
        .materialGeometryStatesGraphOwned = gbufferPayload.materialGeometryStatesGraphOwned,
    };
    OpaqueComputeEmulationProducerBuilder computeEmulationProducerBuilder(context.graph);
    if(opaqueComputeEmulationOutputStatesGraphOwned){
        const auto producerTask = computeEmulationProducerBuilder.declareRegular(computeEmulationProducerInputs, opaqueComputeEmulationOutputUavSetUse, gbufferDependency, opaqueComputeEmulationPayload);
        if(!producerTask)
            return MakeUnexpected(Failure{});
        m_graphicsPrefixOpaqueComputeEmulationTask = *producerTask;
        gbufferDependency = m_graphicsPrefixOpaqueComputeEmulationTask;
    }
    if(opaqueCsgReceiverComputeEmulationOutputStatesGraphOwned){
        const auto producerTask = computeEmulationProducerBuilder.declareCsgReceiver(computeEmulationProducerInputs, csgResources, opaqueCsgReceiverComputeEmulationOutputUavSetUse, gbufferDependency, opaqueCsgReceiverComputeEmulationPayload);
        if(!producerTask)
            return MakeUnexpected(Failure{});
        m_graphicsPrefixOpaqueCsgReceiverComputeEmulationTask = *producerTask;
        gbufferDependency = m_graphicsPrefixOpaqueCsgReceiverComputeEmulationTask;
    }
    Core::GpuTaskSchedulingHint gbufferScheduling;
    gbufferScheduling.cost = Core::GpuTaskCostHint::Medium;
    gbufferScheduling.forceSubmissionBoundary = false;
    gbufferScheduling.allowPacketMerge = true;
    gbufferScheduling.mergeWithPrevious = true;

    // Explicit immediate predecessor. Keep its UAV-to-vertex/index handoff in the primary-Graphics packet despite cross-queue consumers.
    gbufferScheduling.allowMergeAcrossConsumerFrontier = opaqueComputeEmulationOutputStatesGraphOwned || opaqueSharedComputeEmulationOutputStatesGraphOwned || opaqueCsgReceiverComputeEmulationOutputStatesGraphOwned;
    Core::GpuTaskDesc gbufferDesc;
    gbufferDesc
        .setIdentity(Name(s_FrameGraphNameRenderGraphicsPrefixGbuffer))
        .setMarkerLabel("Opaque G-Buffer")
        .setScheduling(gbufferScheduling)
        .setDependencies({ &gbufferDependency, 1u })
        .setResourceUses(gbufferResourceUses)
        .setResourceSetUses({ gbufferMaterialResourceSetUseCount != 0u ? gbufferMaterialResourceSetUses.data() : nullptr, gbufferMaterialResourceSetUseCount })
    ;
    // Successors declare after G-buffer moves its payload. Keep frozen readiness/state bits by value, not from the moved payload.
    const b8 opaqueSharedMaterialDrawBuffersUploaded = gbufferPayload.materialDrawBuffersUploaded;
    const b8 opaqueSharedMaterialFrameStatesGraphOwned = gbufferPayload.materialFrameStatesGraphOwned;
    const b8 opaqueSharedMaterialGeometryStatesGraphOwned = gbufferPayload.materialGeometryStatesGraphOwned;
    const auto graphicsPrefixGbufferTaskResult = context.graph.addTask<ECSRenderDetail::GbufferGraphTask>(gbufferDesc, Move(gbufferPayload));
    if(!graphicsPrefixGbufferTaskResult){
        m_graphicsPrefixGbufferTask = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare opaque G-buffer task"));
        return MakeUnexpected(Failure{});
    }
    m_graphicsPrefixGbufferTask = *graphicsPrefixGbufferTaskResult;

    Core::GpuTaskId gbufferCompletionTask = m_graphicsPrefixGbufferTask;
    if(opaqueSharedComputeEmulationOutputStatesGraphOwned){
        OpaqueSharedComputeEmulationChainBuilder sharedComputeEmulationChainBuilder(context.graph, context.material);
        const auto sharedComputeEmulationChain = sharedComputeEmulationChainBuilder.declare(
            OpaqueSharedComputeEmulationChainInputs{
                .targets = deferredTargets,
                .resources = resources,
                .frameBindings = frameBindings,
                .plan = opaqueSharedComputeEmulationPlan,
                .objectGeometryReads = objectGeometryReads,
                .timingTicket = *timingTicketSlot(DeferredGraphicsPrefixTimingSlot::Gbuffer),
                .meshViewSetupReady = m_graphicsPrefixMeshViewSetupReady,
                .sceneShadingSetupReady = m_graphicsPrefixSceneShadingSetupReady,
                .sharedComputeEmulationTiming = opaqueRegularSharedComputeEmulationTiming,
                .materialGeometrySetUse = gbufferMaterialGeometrySetUse,
                .materialSampledTextureSetUse = gbufferMaterialSampledTextureSetUse,
                .materialInstanceRange = materialInstanceRange,
                .materialTypedRange = materialTypedRange,
                .sharedOutput = opaqueSharedComputeEmulationOutput,
                .gbufferTask = m_graphicsPrefixGbufferTask,
                .instanceCount = instanceData.size(),
                .materialTypedByteCount = materialTypedBytes.size(),
                .materialDrawBuffersUploaded = opaqueSharedMaterialDrawBuffersUploaded,
                .materialFrameStatesGraphOwned = opaqueSharedMaterialFrameStatesGraphOwned,
                .materialGeometryStatesGraphOwned = opaqueSharedMaterialGeometryStatesGraphOwned,
            },
            scratchArena,
            m_graphicsPrefixOpaqueSharedComputeEmulationTasks
        );
        if(!sharedComputeEmulationChain)
            return MakeUnexpected(Failure{});
        m_graphicsPrefixOpaqueSharedComputeEmulationTaskCount = sharedComputeEmulationChain->taskCount;
        gbufferCompletionTask = sharedComputeEmulationChain->completionTask;
    }
    if(hasOpaqueCsgFrameWork){
        const auto opaqueCsgTasks = opaqueCsgIntervalStage.declare(
            OpaqueCsgIntervalStageInputs{
                .targets = deferredTargets,
                .resources = resources,
                .frameBindings = frameBindings,
                .csgResources = csgResources,
                .opaqueDrawItems = opaqueDrawItems,
                .csgFrameData = csgFrameData,
                .intervalSampleTimingTicket = timingTicketSlot(DeferredGraphicsPrefixTimingSlot::CsgIntervalSample),
                .removedIntervalSubresources = csgRemovedIntervalSubresources,
                .removedIntervalCountSubresources = csgRemovedIntervalCountSubresources,
                .materialInstanceRange = materialInstanceRange,
                .materialTypedRange = materialTypedRange,
                .csgReceiverRange = csgReceiverRange,
                .csgCutterRange = csgCutterRange,
                .dependencyTask = gbufferCompletionTask,
                .instanceCount = instanceData.size(),
                .materialTypedByteCount = materialTypedBytes.size(),
                .hasOpaqueDrawItems = hasOpaqueDrawItems,
                .hasCsgFrameGpuWork = hasCsgFrameGpuWork,
            },
            scratchArena,
            objectGeometry
        );
        if(!opaqueCsgTasks)
            return MakeUnexpected(Failure{});
        m_graphicsPrefixCsgReceiverSpanTask = opaqueCsgTasks->receiverSpanTask;
        m_graphicsPrefixCsgIntervalCombineTask = opaqueCsgTasks->intervalCombineTask;
        m_graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationTask = opaqueCsgTasks->intervalSampleComputeEmulationTask;
        m_graphicsPrefixCsgIntervalSampleTask = opaqueCsgTasks->intervalSampleTask;
        gbufferCompletionTask = m_graphicsPrefixCsgIntervalSampleTask;
    }

    PostGbufferNormalizeBuilder normalizeBuilder(context.graph, context.rayTracing);
    const auto graphicsPrefixTaskResult = normalizeBuilder.declare(
        PostGbufferNormalizeInputs{
            .resources = resources,
            .shadowTraceGeometryResources = shadowTraceGeometryResources,
            .shadowTraceGeometrySet = shadowTraceGeometrySet,
            .gbufferCompletionTask = gbufferCompletionTask,
            .shadowVisibilityTask = context.shadowVisibilityTask,
            .asyncPrefixTiming = asyncPrefixTiming,
            .timingTicket = *timingTicketSlot(DeferredGraphicsPrefixTimingSlot::Normalize),
        },
        scratchArena
    );
    if(!graphicsPrefixTaskResult){
        m_graphicsPrefixTask = {};
        return MakeUnexpected(Failure{});
    }
    m_graphicsPrefixTask = *graphicsPrefixTaskResult;
    // Publish only after every prefix task accepts declaration. The caller freezes dependent ray-tracing routes immediately after this successful return.
    context.rayTracing.publishPreparedLightingClassification(rayTracingLightingClassification, Span<const SceneLightGpuData>(prefixSceneUploadResult->lightData.data(), sceneLightCount));
    return prefixSceneUploadResult->sceneLightingContentHash;
}


void GraphicsPrefixGraphCoordinator::resetGraphicsPrefixTaskState()noexcept{
    m_graphicsPrefixOpaqueComputeEmulationTask = {};
    for(Core::GpuTaskId& task : m_graphicsPrefixOpaqueSharedComputeEmulationTasks)
        task = {};
    m_graphicsPrefixOpaqueSharedComputeEmulationTaskCount = 0u;
    m_graphicsPrefixOpaqueCsgReceiverComputeEmulationTask = {};
    m_graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationTask = {};
    m_graphicsPrefixGbufferTask = {};
    m_graphicsPrefixCsgReceiverSpanTask = {};
    m_graphicsPrefixCsgIntervalCombineTask = {};
    m_graphicsPrefixCsgIntervalSampleTask = {};
    m_graphicsPrefixTask = {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
