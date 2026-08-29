// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/renderer_frame_pipeline.h>

#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/kernel/renderer_private.h>

#include <core/graphics/gpu_timing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RendererFramePipeline::RendererFramePipeline(
    Core::Alloc::GlobalArena& arena,
    Core::ECS::World& world,
    Core::Graphics& graphics,
    Core::Assets::AssetManager& assetManager,
    ShaderPathResolveCallback shaderPathResolver
)
    : m_arena(arena)
    , m_world(world)
    , m_graphics(graphics)
    , m_assetManager(assetManager)
    , m_shaderPathResolver(Move(shaderPathResolver))
    , m_csgShapeRegistry(arena)
    , m_frameGraphRendererLabel(arena)
    , m_deferredLightingTaskGraph(arena)
    , m_deferredLightingTaskGraphAnalysis(arena)
    , m_deferredLightingTaskGraphQueueAssignments(arena)
    , m_deferredLightingTaskGraphQueueAssignmentTelemetry(arena)
    , m_deferredLightingCompiledGraph(arena)
    , m_deferredLightingRecordedGraph(arena)
    , m_deferredLightingSubmissionTransaction(arena)
    , m_deferredTaskTimingFeedback(arena, graphics)
    , m_meshState(arena)
    , m_materialState(arena)
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
    , m_meshSystem(arena, world, graphics, assetManager, m_meshState, m_drawState)
    , m_csgSystem(
        arena,
        world,
        graphics,
        m_csgShapeRegistry,
        m_drawState,
        m_csgState,
        m_shaderSystem,
        m_meshSystem
    )
    , m_materialSystem(
        arena,
        world,
        graphics,
        assetManager,
        m_csgShapeRegistry,
        m_materialState,
        m_drawState,
        m_shaderSystem,
        m_meshSystem,
        m_csgSystem
    )
    , m_deferredSystem(arena, world, graphics, m_deferredState, m_rayTracingState, m_shaderSystem)
    , m_avboitSystem(
        arena,
        graphics,
        m_avboitState,
        m_shaderSystem,
        m_materialSystem,
        m_csgSystem
    )
    , m_raytracingSystem(
        arena,
        world,
        graphics,
        m_shaderSystem,
        m_meshSystem,
        m_materialSystem,
        m_meshState,
        m_drawState,
        m_deferredState,
        m_rayTracingState
    )
{
    if(!RegisterBuiltInCsgShapeTypes(m_csgShapeRegistry))
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register built-in CSG shape types"));

    m_deferredTaskTimingFeedback.activate();
}
RendererFramePipeline::~RendererFramePipeline(){
    m_deferredTaskTimingFeedback.deactivate();
}


bool RendererFramePipeline::setTaskGraphTimingFeedbackPolicy(const Core::GpuTaskTimingFeedbackPolicy& policy){
    return m_deferredTaskTimingFeedback.setPolicy(policy);
}


Core::GpuTaskGraphRuntimeStatistics RendererFramePipeline::deferredTaskGraphRuntimeStatistics()const noexcept{
    return Core::CollectGpuTaskGraphRuntimeStatistics(
        m_deferredLightingCompiledGraph,
        m_deferredLightingRecordedGraph,
        m_deferredLightingSubmissionTransaction
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererFramePipeline::reportLaggedLightingTransition(const LaggedLightingReport report, const u64 targetGeneration){
    if(m_laggedLightingReport == report && m_laggedLightingReportGeneration == targetGeneration)
        return;

    m_laggedLightingReport = report;
    m_laggedLightingReportGeneration = targetGeneration;
    switch(report){
    case LaggedLightingReport::Unreported:
        break;
    case LaggedLightingReport::NoDedicatedAsyncCompute:
        NWB_LOGGER_ESSENTIAL_INFO(
            NWB_TEXT("RendererSystem: frame-lagged async lighting Graphics queue route accepted (no dedicated Compute queue, target generation {})"),
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

void RendererFramePipeline::invalidateLaggedLightingHistorySubmission()noexcept{
    m_laggedLightingHistorySubmissionToken = Core::QueueSubmissionToken{};
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

void RendererFramePipeline::resetTargetGenerationStateHandoffs()noexcept{
    // Replaced targets invalidate retained compute-local state.
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
    // The shadow-preparation packet owns its serial export; only retained cross-frame state is reset here.
    resetTargetGenerationStateHandoffs();
    m_shadowPreparePersistentState.reset();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

