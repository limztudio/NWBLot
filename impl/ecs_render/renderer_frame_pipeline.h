// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/components.adhd.h>
#include <impl/ecs_render/avboit/renderer_avboit_state.h>
#include <impl/ecs_render/avboit/avboit_system.h>
#include <impl/ecs_render/csg/csg_system.h>
#include <impl/ecs_render/csg/renderer_csg_state.h>
#include <impl/ecs_render/deferred/deferred_system.h>
#include <impl/ecs_render/deferred/renderer_deferred_state.h>
#include <impl/ecs_render/gi/quality_settings.adhd.h>
#include <impl/ecs_render/kernel/task_timing_feedback.h>
#include <impl/ecs_render/material/material_instance.h>
#include <impl/ecs_render/material/renderer_material_state.h>
#include <impl/ecs_render/material/material_system.h>
#include <impl/ecs_render/mesh/mesh_system.h>
#include <impl/ecs_render/mesh/renderer_mesh_state.h>
#include <impl/ecs_render/optics/coincident_volumes.h>
#include <impl/ecs_render/reflection/reflection_system.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <impl/ecs_render/raytrace/raytracing_system.h>
#include <impl/ecs_render/shader/shader_system.h>
#include <impl/ecs_render/shared/task_graph_stage.h>
#include <impl/assets/graphics/mesh/binding_slots.h>
#include <impl/assets_graphics/csg/subsystem.adhd.h>
#include <impl/assets_graphics/renderer/subsystem.adhd.h>
#include <impl/assets_graphics/transparent/subsystem.adhd.h>
#include <impl/assets_material/asset.h>
#include <impl/assets_scene/asset.h>
#include <impl/ecs_csg/frame_state.h>
#include <impl/ecs_csg/shape_registry.h>

#include <core/ecs/system.h>
#include <core/graphics/gpu_timing.h>
#include <core/graphics/runtime/render_pass.h>
#include <core/task/gpu/compiler.h>
#include <core/task/gpu/packet_runtime.h>
#include <core/task/gpu/persistent_state.h>
#include <core/task/gpu/presentation_contributor.h>
#include <core/task/gpu/output_layer_contributor.h>
#include <core/task/gpu/queue_assignment_telemetry.h>
#include <core/telemetry/frame_graph_contributor.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class AssetManager;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ObjectGeometryCacheGraph;
class Mesh;
struct DeferredGraphResourceImportResult;
struct GraphClearTimingRecordState;
struct RayTracingSceneGraphReads;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// These semantic prefix stages may coalesce into one native submission or split at a compiler-derived cross-queue frontier.
// Each stage points at a rebindable timing slot so the renderer can attach one ticket to every actual packet after compilation.
namespace DeferredGraphicsPrefixTimingSlot{
    enum Enum : u8{
        MeshViewSetup,
        SceneShadingSetup,
        DeferredClear,
        Gbuffer,
        CsgReceiverSpanBuild,
        CsgIntervalCombine,
        CsgIntervalSample,
        Normalize,
        Count,
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererFramePipeline final : NoCopy{
    friend class OpaqueEmulationMergeValidator;
    friend class FrameExecuteCpuRestore;
    friend class FrameExecuteHistoryCopy;
    friend class FrameExecuteLifecycle;
    friend class FrameExecutePresentTails;
    friend class FrameExecuteRecovery;
    friend struct FrameExecuteStateLifecycleSet;
    friend struct FrameExecuteTimingState;
    friend class FrameExecuteSurfelReadback;
    friend class ShadowPreparePacketValidator;
    friend class SurfelCausticsMergeValidator;
    friend class GraphicsPrefixTimingResolver;
    friend class ShadowVisibilityMergeValidator;


private:
    // Transition reports observe accepted-history lifecycle decisions without owning them.
    struct LaggedLightingReport{
        enum Enum : u8{
            Unreported,
            NoDedicatedAsyncCompute,
            BootstrapAccepted,
            ActiveHistoryAccepted,
            CurrentFrameAccepted,
        };
    };


public:
    using ShaderPathResolveCallback = RendererShaderPathResolveCallback;


public:
    RendererFramePipeline(Core::Alloc::GlobalArena& arena, Core::ECS::World& world, Core::GraphicsRuntime& graphics, Core::Assets::AssetManager& assetManager, ShaderPathResolveCallback shaderPathResolver);
    ~RendererFramePipeline();


public:
    [[nodiscard]] bool validateResources(u32 width, u32 height, u32 sampleCount);
    void invalidateResources();

    void update(Core::ECS::World& world, f32 delta)noexcept;

    [[nodiscard]] bool prepareResources(Core::Framebuffer* framebuffer);
    void render(Core::Framebuffer* framebuffer);
    [[nodiscard]] bool appendFrameGraph(Core::Telemetry::FrameGraphBuilder& builder);
    void setActiveSceneSubsystems(const ActiveSceneSubsystems* subsystems)noexcept{ m_activeSceneSubsystems = subsystems; m_shaderSystem.setActiveSceneSubsystems(subsystems); }
    [[nodiscard]] bool isTransparentFeatureEnabled()const noexcept{
        return m_activeSceneSubsystems && m_activeSceneSubsystems->hasSubsystem<TransparentSubsystem>();
    }
    [[nodiscard]] bool isCsgFeatureEnabled()const noexcept{
        return m_activeSceneSubsystems && m_activeSceneSubsystems->hasSubsystem<CsgSubsystem>();
    }
    // Trades one frame of effect latency for overlap; off by default, self-bootstraps on toggle.
    void setRefractionEnabled(const bool enabled)noexcept{ m_refractionEnabled = enabled; }

    void setRefractionDiagnosticsEnabled(const bool enabled)noexcept{ m_raytracingSystem.setRefractionDiagnosticsEnabled(enabled); }
    [[nodiscard]] Expected<RefractionStatistics> tryGetLatestRefractionStatistics()const noexcept;

    void setRefractionHardwareTracingEnabled(const bool enabled)noexcept{
        m_raytracingSystem.setRefractionHardwareTracingEnabled(enabled);
    }

    [[nodiscard]] bool setCausticQualitySettings(const CausticQualitySettings& settings)noexcept{
        return m_raytracingSystem.setCausticQualitySettings(settings);
    }

    [[nodiscard]] bool setSurfelGiQualitySettings(const SurfelGiQualitySettings& settings)noexcept{
        if(!ValidateSurfelGiQualitySettings(settings))
            return false;
        m_surfelGiQualitySettings = settings;
        return true;
    }

    [[nodiscard]] bool setSoftwareShadowSettings(const SoftwareShadowSettings& settings)noexcept{
        return m_raytracingSystem.setSoftwareShadowSettings(settings);
    }

    [[nodiscard]] bool setShadowQualitySettings(const ShadowQualitySettings& settings)noexcept{
        return m_raytracingSystem.setShadowQualitySettings(settings);
    }
    [[nodiscard]] bool setReflectionSettings(const ReflectionSettings& settings)noexcept;
    [[nodiscard]] Expected<ReflectionStatistics> tryGetLatestReflectionStatistics()const noexcept;
    [[nodiscard]] bool setPresentationSettings(const PresentationSettings& settings)noexcept;
    void setFrameLaggedAsyncLightingEnabled(const bool enabled)noexcept{
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

    [[nodiscard]] bool frameLaggedAsyncLightingEnabled()const noexcept{ return m_frameLaggedAsyncLightingEnabled; }
    [[nodiscard]] bool setTaskGraphTimingFeedbackPolicy(const Core::GpuTaskTimingFeedbackPolicy& policy);
    // Telemetry for current deferred graph artifacts. Valid until the next graph/recording/transaction reset.
    [[nodiscard]] Core::GpuTaskGraphRuntimeStatistics deferredTaskGraphRuntimeStatistics()const noexcept;


private:
    [[nodiscard]] bool prepareGpuTimingScopes();
    [[nodiscard]] bool prepareDeferredGraphPacketEnvelopeMetrics(const Core::GpuTaskGraph::DeclarationReadView& graph, const Core::GpuCompiledGraph::ReadView& compiledGraph, Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] bool declareFrameTimingBeginTask(Core::GpuTimingFrameTransaction& frameTimingTransaction);
    [[nodiscard]] bool scheduleDeferredLightingTaskGraphForExecution(Core::Alloc::ScratchArena& scratchArena);
    void commitFrameTargets(DeferredFrameTargets&& targets);
    void resetFrameTargets();
    void resetDeferredTaskGraphRuntime();
    void resetFrameTaskState();
    void resetShadowPrepareUploadTaskState()noexcept;
    void resetShadowPrepareTaskState()noexcept;
    void resetDeferredFrameTaskIdsAndFlags()noexcept;
    void resetDeferredCausticsTaskState()noexcept;
    void resetSharedDeferredFrameTaskState()noexcept;
    void resetGraphicsPrefixTaskState()noexcept;
    // Accepted cross-frame state survives ordinary recording attempts. Reset only when the target/resource generation changes.
    void resetTargetGenerationStateHandoffs()noexcept;
    void resetInvalidatedResourceStateHandoffs()noexcept;
    void invalidateLaggedLightingHistorySubmission()noexcept;
    void invalidateLaggedLightingHistoryWriterDrain()noexcept;
    void resetLaggedLightingHistoryReadTracking()noexcept;
    void resetLaggedLightingHistoryTracking()noexcept;
    [[nodiscard]] bool declareDeferredShadowPrepareTask(
        const RayTracingShadowPreparationResourceSnapshot& rayTracingShadowResources,
        const RayTracingDeferredGraphResourceSnapshot& rayTracingResources,
        Core::GpuGraphResourceId currentBindlessSlots,
        Core::GpuGraphResourceId materialContextSlots,
        Span<const Core::GpuGraphResourceId> shadowTraceGeometryResources,
        Span<const Core::GpuGraphResourceId> softwareBvhBuildStateResources,
        bool softwareTraceResourcesPrepared,
        Core::Alloc::ScratchArena& scratchArena,
        DeferredFrameTargets& deferredTargets,
        Core::GpuTimingSubmissionTicket& timingTicket
    );
    [[nodiscard]] Expected<u64> declareDeferredGraphicsPrefixTasks(
        Core::GpuTaskId shadowPrepareTask,
        const CsgFrameState& csgFrameState,
        const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings,
        const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources,
        bool hasOpaqueCsgFrameWork,
        f32 meshViewAspectRatio,
        const ECSRenderDetail::MeshViewGpuData& meshViewState,
        bool meshViewUploadRequired,
        const DeferredGraphResourceImportResult& resources,
        Span<const Core::GpuGraphResourceId> shadowTraceGeometryResources,
        Core::GpuGraphResourceSetId shadowTraceGeometrySet,
        const bool& asyncPrefixTimingSpansOnePacket,
        Core::Alloc::ScratchArena& scratchArena,
        DeferredFrameTargets& deferredTargets,
        ObjectGeometryCacheGraph& objectGeometry,
        Optional<Core::GpuTimingMeasure>& asyncPrefixTiming,
        Optional<Core::GpuTimingMeasure>& deferredClearTiming,
        GraphClearTimingRecordState& deferredClearTimingState,
        GraphClearTimingRecordState& csgIntervalClearTimingState,
        Optional<Core::GpuTimingMeasure>& opaqueRegularSharedComputeEmulationTiming,
        Optional<Core::GpuTimingMeasure>& opaqueCsgIntervalSampleComputeEmulationTiming,
        Core::GpuTimingSubmissionTicket** timingTickets
    );
    [[nodiscard]] bool declareDeferredShadowVisibilityTask(
        const DeferredLightingGraphResources& deferredLightingResources,
        const RayTracingShadowPreparationResourceSnapshot& rayTracingShadowResources,
        const RayTracingDeferredGraphResourceSnapshot& rayTracingResources,
        const RayTracingShadowVisibilityGraphPlanSnapshot& rayTracingPlan,
        const RayTracingSceneGraphReads& hardwareSceneReads,
        bool hardwareShadowSupported,
        Core::GpuGraphResourceId worldPosition,
        Core::GpuGraphResourceId normal,
        Core::GpuGraphResourceId depth,
        Core::GpuGraphResourceId shadowVisibility,
        Core::GpuGraphResourceId currentBindlessSlots,
        Core::GpuGraphResourceId sceneShading,
        Core::GpuGraphResourceId lights,
        Core::GpuGraphResourceId materialContextSlots,
        Span<const Core::GpuGraphResourceId> traceGeometryResources,
        Core::GpuGraphResourceSetId traceGeometrySet,
        Core::GpuGraphResourceSetId traceMaterialSampledTextureSet,
        Core::GpuTaskId prefixTask,
        Core::GpuExternalCompletionId laggedLightingHistoryWriterDrainCompletion,
        Core::Alloc::ScratchArena& scratchArena,
        DeferredFrameTargets& deferredTargets,
        Core::GpuTimingSubmissionTicket& timingTicket,
        Optional<Core::GpuTimingMeasure>& asyncTiming,
        Optional<Core::GpuTimingMeasure>& shadowVisibilityTiming,
        Optional<Core::GpuTimingMeasure>& opaqueResolveTiming,
        Optional<Core::GpuTimingMeasure>& transparentResolveTiming,
        bool& opaqueProduced,
        bool& transparentTraceProduced,
        u32& opaqueFrameIndex
    );
    [[nodiscard]] bool declareDeferredSoftwareCausticsTask(
        bool hardwareCaustics,
        const DeferredLightingGraphResources& deferredLightingResources,
        const RayTracingDeferredGraphResourceSnapshot& rayTracingResources,
        Core::GpuGraphResourceId worldPosition,
        Core::GpuGraphResourceId depth,
        Core::GpuGraphResourceId causticIrradiance,
        Core::GpuGraphResourceId currentBindlessSlots,
        Core::GpuGraphResourceId sceneShading,
        Core::GpuGraphResourceId lights,
        Core::GpuGraphResourceId materialContextSlots,
        Core::GpuGraphResourceId opticalScene,
        Span<const Core::GpuGraphResourceId> softwareTraceGeometryResources,
        Core::GpuGraphResourceSetId softwareTraceGeometrySet,
        Core::GpuGraphResourceSetId traceMaterialSampledTextureSet,
        Core::Alloc::ScratchArena& scratchArena,
        DeferredFrameTargets& deferredTargets,
        Core::GpuTimingSubmissionTicket& timingTicket,
        Optional<Core::GpuTimingMeasure>& causticPhotonTiming,
        Optional<Core::GpuTimingMeasure>& causticResolveTiming
    );
    [[nodiscard]] bool declareDeferredSurfelGiTask(
        const DeferredLightingGraphResources& deferredLightingResources,
        const RayTracingDeferredGraphResourceSnapshot& rayTracingResources,
        const RayTracingSurfelPersistentResourceSnapshot& rayTracingSurfelResources,
        Core::GpuGraphResourceId worldPosition,
        Core::GpuGraphResourceId normal,
        Core::GpuGraphResourceId surfelIrradiance,
        Core::GpuGraphResourceId currentBindlessSlots,
        Core::GpuGraphResourceId sceneShading,
        Core::GpuGraphResourceId lights,
        Core::GpuGraphResourceId materialContextSlots,
        Span<const Core::GpuGraphResourceId> traceGeometryResources,
        Core::GpuGraphResourceSetId traceGeometrySet,
        Core::GpuGraphResourceSetId traceMaterialSampledTextureSet,
        Core::GpuTaskId effectsTask,
        bool useLaggedLightingHistory,
        Core::GpuExternalCompletionId surfelCounterReadbackCompletion,
        Core::Alloc::ScratchArena& scratchArena,
        DeferredFrameTargets& deferredTargets,
        Core::GpuTimingSubmissionTicket& timingTicket,
        Optional<Core::GpuTimingMeasure>& asyncTiming
    );
    void declareDeferredSurfelCountReadbackTask(const RayTracingSurfelPersistentResourceSnapshot& rayTracingSurfelResources);
    void buildDeferredLightingTaskGraph(
        Core::Alloc::ScratchArena& scratchArena,
        const ECSRenderDetail::RendererFrameGraphFeatures& features,
        const CsgFrameState& csgFrameState,
        bool clearAvboitTargets,
        bool hasTransparentRenderers,
        bool hasOpaqueCsgFrameWork,
        f32 meshViewAspectRatio,
        const Core::AcquiredPresentationFrame& presentationFrame,
        const bool& asyncPrefixTimingSpansOnePacket,
        const Core::QueueSubmissionToken& surfelCounterReadbackCompletionToken,
        const Core::QueueSubmissionToken& laggedLightingHistoryReadReadyToken,
        const Core::QueueSubmissionToken& laggedLightingHistoryWriterDrainToken,
        bool includeLaggedLightingHistoryCapture,
        DeferredFrameTargets& deferredTargets,
        Core::GpuTimingFrameTransaction& frameTimingTransaction,
        Optional<Core::GpuTimingMeasure>& asyncPrefixTiming,
        Optional<Core::GpuTimingMeasure>& deferredClearTiming,
        GraphClearTimingRecordState& deferredClearTimingState,
        GraphClearTimingRecordState& opaqueCsgIntervalClearTimingState,
        Optional<Core::GpuTimingMeasure>& opaqueRegularSharedComputeEmulationTiming,
        Optional<Core::GpuTimingMeasure>& opaqueCsgIntervalSampleComputeEmulationTiming,
        Core::GpuTimingSubmissionTicket& shadowPrepareTimingTicket,
        Core::GpuTimingSubmissionTicket** graphicsPrefixTimingTickets,
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
        bool& shadowVisibilityOpaqueProduced,
        bool& shadowVisibilityTransparentTraceProduced,
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
    );
    void reportLaggedLightingTransition(LaggedLightingReport::Enum report, u64 targetGeneration);


private:
    Core::Alloc::GlobalArena& m_arena;
    Core::ECS::World& m_world;
    Core::GraphicsRuntime& m_graphics;
    Core::Assets::AssetManager& m_assetManager;
    ShaderPathResolveCallback m_shaderPathResolver;
    CsgShapeRegistry m_csgShapeRegistry;
    // A successful graphics tick can skip renderer execution while telemetry still captures.
    // Keep the exact source frame separate from graph generations so retained prior-frame runtime statistics never look current.
    u64 m_frameGraphSourceFrameIndex = Limit<u64>::s_Max;
    // Storage must outlive appendFrameGraph(); FrameGraphBuilder keeps labels by view until capture is encoded.
    AString<Core::Alloc::GlobalArena> m_frameGraphRendererLabel;
    // Optional selector upload. Merges into Shadow Preparation's first Graphics packet; acceptance commits the CPU residency bit with the first consumer.
    Core::GpuTaskId m_deferredBindlessSlotsUploadTask;
    // Descriptor-slot snapshot. Merges into the same first Graphics packet; later Compute trace consumers inherit Shadow Preparation's handoff.
    Core::GpuTaskId m_rayTraceMaterialContextSlotsUploadTask;
    Core::GpuTaskId m_csgTraceContextUploadTask;
    // Optional refractive-AABB stream. Merges into that same first Graphics packet; caustic consumers inherit Shadow Preparation's handoff.
    Core::GpuTaskId m_causticEmissionTargetsUploadTask;
    // Optional per-frame surfel constant payload; merges into the first Graphics packet alongside Shadow Preparation.
    Core::GpuTaskId m_surfelFrameConstantsUploadTask;
    // Optional ABI-coupled shadow material context batch; merges into Shadow Preparation so its SRV handoff owns later trace consumers.
    Core::GpuTaskId m_shadowInstanceMaterialUploadTask;
    Core::GpuTaskId m_shadowInstanceUploadTask;
    Core::GpuTaskId m_shadowMaterialTypedUploadTask;
    // CPU-built software scene-BVH pair. Both must merge into Shadow Preparation's packet before it owns the later handoff.
    Core::GpuTaskId m_sceneBvhNodesUploadTask;
    Core::GpuTaskId m_sceneBvhInstancesUploadTask;
    // Lagged-history selector upload. Merges into Deferred Lighting's packet, which already owns history acceptance and the prior-frame wait.
    Core::GpuTaskId m_deferredLaggedLightingHistorySlotsUploadTask;
    Core::GpuTaskId m_deferredFrameTimingBeginTask;
    Core::GpuTaskId m_deferredShadowPrepareTask;
    // Per-mesh software builds lower sentinel clears and callbacks before Shadow Preparation's scene-build endpoint. Both bounds stay in that packet.
    Core::GpuTaskId m_deferredShadowPrepareSoftwareBvhBuildFirstTask;
    Core::GpuTaskId m_deferredShadowPrepareSoftwareBvhBuildLastTask;
    Core::GpuTaskId m_deferredShadowPrepareSceneRefitTask;
    Core::GpuTaskId m_sceneBvhRefitInputsUploadTask;
    // TLAS/BLAS builds record in Shadow Preparation; this adjacent callback publishes their AccelStructRead bounds in the same first packet.
    Core::GpuTaskId m_deferredShadowPrepareAccelStructFinalizeTask;
    Core::GpuTaskId m_graphicsPrefixMeshViewSetupTask;
    Core::GpuTaskId m_graphicsPrefixSceneShadingSetupTask;
    // LOAD_OP_CLEAR work counts as G-buffer timing. Deferred-clear timing covers only this opaque-color UAV clear, whose hooks share one graph task.
    Core::GpuTaskId m_graphicsPrefixDeferredClearTask;
    // Opaque CSG work-region clear is a two-value typed rectangle chain. Both tasks stay in one Graphics packet to keep the CSG-clear timing interval.
    Core::GpuTaskId m_graphicsPrefixCsgIntervalClearFirstTask;
    Core::GpuTaskId m_graphicsPrefixCsgIntervalClearTask;
    // Opaque compute-emulation outputs dispatch before G-buffer rasterization. This producer shares G-buffer's primary packet.
    Core::GpuTaskId m_graphicsPrefixOpaqueComputeEmulationTask;
    // Small shared-output regular paths keep dispatch/raster alternation in the same packet.
    // Keep every phase ID so validation can prove the strict D(A) -> R(A) -> ... order, not just endpoint coalescing.
    // The active prefix holds four, six, eight, or ten phases for two through five draws.
    Core::GpuTaskId m_graphicsPrefixOpaqueSharedComputeEmulationTasks[ECSRenderDetail::s_SharedComputeEmulationMaximumPhaseCount] = {};
    usize m_graphicsPrefixOpaqueSharedComputeEmulationTaskCount = 0u;
    // Receiver-surface CSG has its own readiness gate but needs the same packet-local output handoff.
    Core::GpuTaskId m_graphicsPrefixOpaqueCsgReceiverComputeEmulationTask;
    // Interval-sample CSG follows Combine. Both IDs stay in that primary Graphics packet with the existing CSG Interval Sample consumer.
    Core::GpuTaskId m_graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationTask;
    Core::GpuTaskId m_graphicsPrefixGbufferTask;
    Core::GpuTaskId m_graphicsPrefixCsgReceiverSpanTask;
    Core::GpuTaskId m_graphicsPrefixCsgIntervalCombineTask;
    Core::GpuTaskId m_graphicsPrefixCsgIntervalSampleTask;
    Core::GpuTaskId m_graphicsPrefixTask;
    // Soft-transparent shadow frames compile opaque production, wavelets, tails, transparent trace, merge, and terminal tail into one packet.
    Core::GpuTaskId m_deferredShadowVisibilityOpaqueTask;
    Core::GpuTaskId m_deferredShadowVisibilityOpaqueFirstWaveletTask;
    Core::GpuTaskId m_deferredShadowVisibilityOpaqueResolveTask;
    Core::GpuTaskId m_deferredShadowVisibilityTransparentTraceTask;
    Core::GpuTaskId m_deferredShadowVisibilityTransparentTemporalMergeTask;
    Core::GpuTaskId m_deferredShadowVisibilityTransparentFirstWaveletTask;
    Core::GpuTaskId m_deferredShadowVisibilityDiagnosticsTask;
    // The adaptive append-counter clear shares the monolithic visibility packet to keep its CopyDest -> UAV handoff graph-owned.
    Core::GpuTaskId m_deferredShadowVisibilityAdaptiveCounterClearTask;
    // The monolithic route clears visibility to all-lit right before its callback, in the same packet to keep handoff and acceptance graph-owned.
    Core::GpuTaskId m_deferredShadowVisibilityAllLitClearTask;
    Core::GpuTaskId m_deferredShadowVisibilityTask;
    Core::GpuTaskId m_deferredSoftwareCausticsTask;
    // The HW/SW caustic producer shares this clear's packet to preserve timing and acceptance.
    Core::GpuTaskId m_deferredCausticIrradianceClearTask;
    // The temporal bootstrap is conditional; when present it stays with the selected caustic producer, which commits only on acceptance.
    Core::GpuTaskId m_deferredCausticAccumulatorBootstrapClearTask;
    // Non-temporal caustics reset the accumulator each frame via the producer packet; the CPU reset commits after that packet accepts.
    Core::GpuTaskId m_deferredCausticAccumulatorNonTemporalClearTask;
    // A warm temporal accumulator decays in a mergeable graph task before its selected photon producer.
    Core::GpuTaskId m_deferredCausticAccumulatorDecayTask;
    // Photon, downsample, resolve prepare, wavelets, upsample, and timing close stay in one packet with compiler-owned handoffs.
    Core::GpuTaskId m_deferredCausticPhotonTask;
    Core::GpuTaskId m_deferredCausticGeometryTask;
    Core::GpuTaskId m_deferredCausticResolvePrepareTask;
    Core::GpuTaskId m_deferredCausticResolveWaveletTask;
    Core::GpuTaskId m_deferredCausticResolveSecondWaveletTask;
    Core::GpuTaskId m_deferredCausticResolveThirdWaveletTask;
    Core::GpuTaskId m_deferredCausticResolveFourthWaveletTask;
    Core::GpuTaskId m_deferredCausticResolveFifthWaveletTask;
    Core::GpuTaskId m_deferredCausticResolveUpsampleTask;
    // Surfel lifecycle publishes after all four clears accept; stages share one effects packet.
    Core::GpuTaskId m_deferredSurfelGiPreparationTask;
    Core::GpuTaskId m_deferredSurfelGiInitializationLifecycleTask;
    Core::GpuTaskId m_deferredSurfelGiSnapshotCopyTask;
    Core::GpuTaskId m_deferredSurfelGiIrradianceClearTask;
    Core::GpuTaskId m_deferredSurfelGiAgeFreeTask;
    Core::GpuTaskId m_deferredSurfelGiCellHeadClearTask;
    Core::GpuTaskId m_deferredSurfelGiHashBuildTask;
    Core::GpuTaskId m_deferredSurfelGiSpawnTask;
    Core::GpuTaskId m_deferredSurfelGiTraceBuildArgsTask;
    Core::GpuTaskId m_deferredSurfelGiTraceTask;
    Core::GpuTaskId m_deferredSurfelGiResolveTask;
    Core::GpuTaskId m_deferredSurfelGiTask;
    // A rare diagnostic tail: it depends on GI but records/submits after Present on Transfer when available.
    Core::GpuTaskId m_deferredSurfelGiCounterReadbackTask;
    Core::GpuTaskId m_deferredHardwareCausticsTask;
    Core::GpuTaskId m_deferredLightingTask;
    Core::GpuTaskId m_deferredCompositeTask;
    // Optional final overlay appended by a registered presentation contributor.
    // The deferred scene output remains separately named because lagged-history copies may begin from it while the overlay finishes on Graphics.
    Core::GpuTaskId m_deferredPresentationOverlayTask;
    Core::GpuTaskId m_deferredPresentTask;
    // Terminal Graphics task: records the frame-timing endpoint and owns the swap-chain signal after Present and any contributor.
    Core::GpuTaskId m_deferredFrameTimingEndTask;
    Core::GpuTaskId m_deferredLaggedLightingHistoryTask;
    // Recovery stays unrecorded until a later packet rejects. Its submission join waits for every latest accepted non-Graphics queue.
    Core::GpuTaskId m_deferredFrameRecoveryTask;
    // Imported only while the preceding frame's diagnostic Transfer readback remains in flight.
    Core::GpuExternalCompletionId m_deferredSurfelGiCounterReadbackCompletion;
    // Distinct completions even for the same history-copy token: Lighting waits for readable history, producers wait until the copy stops reading.
    Core::GpuExternalCompletionId m_deferredLightingHistoryReadReadyCompletion;
    Core::GpuExternalCompletionId m_deferredLightingHistoryWriterDrainCompletion;
    bool m_deferredShadowCombinedUpsample = false;
    bool m_deferredShadowCombinedWavelet = false;
    bool m_deferredShadowCombinedTemporal = false;
    bool m_deferredCausticProducerDispatched = false;
    // A partially accepted frame with an unsubmittable recovery packet is not recoverable. End this device generation and rebuild resources.
    bool m_frameRenderRecoveryFailed = false;
    bool m_graphicsPrefixMeshViewSetupReady = false;
    bool m_graphicsPrefixSceneShadingSetupReady = false;
    bool m_deferredFrameRecoveryArmed = false;
    bool m_deferredFrameRecoveryRetiresTiming = false;
    bool m_deferredLightingTaskGraphDeclared = false;
    bool m_deferredLightingTaskGraphScheduled = false;
    bool m_deferredPresentationOverlayRequired = false;
    u32 m_reflectionFrameIndex = 0u;
    f64 m_deferredLightingTaskGraphDeclarationSeconds = 0.0;
    Core::IGpuTaskGraphPresentationContributor* m_preparedTaskGraphPresentationContributor = nullptr;
    Core::IGpuTaskGraphOutputLayerContributor* m_preparedTaskGraphOutputLayerContributor = nullptr;
    ActiveSceneSubsystems m_defaultSceneSubsystems;
    const ActiveSceneSubsystems* m_activeSceneSubsystems = nullptr;

private:
    RendererMeshState m_meshState;
    RendererMaterialState m_materialState;
    RendererOpticalVolumeSelection m_opticalVolumes;
    RendererCsgState m_csgState;
    RendererDeferredState m_deferredState;
    RendererAvboitState m_avboitState;
    u64 m_laggedLightingReportGeneration = 0u;
    RendererRayTracingState m_rayTracingState;
    CsgFrameState m_preparedCsgFrameState;
    // Shadow scratch/history keeps accepted packet state across frames on either compiler route.
    Core::GpuPersistentResourceStateCache m_shadowComputePersistentState;
    Core::GpuPersistentResourceStateCache m_shadowVisibilityReturnState;
    // TLAS/BLAS, software-BVH build storage, and trace geometry change inside Shadow Preparation. Keep only live generations.
    Core::GpuPersistentResourceStateCache m_shadowPreparePersistentState;
    // Software caustics retain their temporal scratch on either compiler-selected route.
    // Hardware dispatch-rays caustics retain their temporal accumulator only after the Graphics hardware-caustics packet accepts.
    Core::GpuPersistentResourceStateCache m_causticsComputePersistentState;
    Core::GpuPersistentResourceStateCache m_hardwareCausticAccumulatorPersistentState;
    // Keep only the accepted cross-queue return state for Graphics lighting back to a Compute-routed caustics packet.
    Core::GpuPersistentResourceStateCache m_causticIrradianceReturnState;
    // Surfel GI retains its field and history on either route; resolved irradiance is consumed directly or snapshotted for lagged lighting.
    Core::GpuPersistentResourceStateCache m_surfelGiComputePersistentState;
    // The counter can continue into a late Transfer readback, so retain the accepted tail state and its typed backing separately.
    // The next Surfel-GI packet imports this cache through its first-use declaration.
    Core::GpuPersistentResourceStateCache m_surfelGiCounterPersistentState;
    Core::GpuPersistentResourceStateCache m_surfelIrradianceReturnState;
    bool m_laggedLightingCurrentFrameAcceptancePending = false;
    bool m_preparedRefractionActive = false;
    bool m_refractionHardwareLogged = false;
    bool m_refractionScreenLogged = false;
    bool m_preparedReflectionSceneAvailable = false;
    bool m_reflectionHardwareLogged = false;
    bool m_reflectionFallbackLogged = false;
    bool m_refractionEnabled = true;
    LaggedLightingReport::Enum m_laggedLightingReport = LaggedLightingReport::Unreported;
    bool m_preparedCsgFrameStateValid = false;
    bool m_preparedHasTransparentRenderers = false;
    ShadowPreparationOutcome m_shadowPreparationOutcome;
    bool m_frameLaggedAsyncLightingEnabled = false;
    SurfelGiQualitySettings m_surfelGiQualitySettings;
    ReflectionSettings m_reflectionSettings;
    RayTracingRefractionGraphResources m_preparedRefractionResources;
    Core::QueueSubmissionToken m_laggedLightingHistorySubmissionToken;
    // Newest incomplete accepted same-generation Transfer tail guards live producer writes while declaration resets the history-read token.
    Core::QueueSubmissionToken m_laggedLightingHistoryWriterDrainToken;
    // Target creation bumps this identity per generation, so a recycled slot cannot make recreated history look accepted.
    u64 m_laggedLightingHistoryGeneration = 0u;
    u64 m_laggedLightingHistoryWriterDrainGeneration = 0u;
    // The root composes this aggregate from Deferred, AVBOIT, CSG, and RayTracing resources.
    // Keep its storage before the domain systems so their service objects are destroyed before the shared frame-resource generation.
    DeferredFrameTargets m_frameTargets;

private:
    RendererShaderSystem m_shaderSystem;
    RendererMeshSystem m_meshSystem;
    RendererCsgSystem m_csgSystem;
    RendererMaterialSystem m_materialSystem;
    RendererDeferredSystem m_deferredSystem;
    RendererReflectionSystem m_reflectionSystem;
    RendererAvboitSystem m_avboitSystem;
    RendererRayTracingSystem m_raytracingSystem;

private:
    // Declare every callback owner before graph storage so reverse destruction keeps graph storage alive during teardown and unwinding.
    RendererTaskTimingFeedback m_deferredTaskTimingFeedback;
    // All renderer stages, presentation, optional history copies, and recovery share one packet graph.
    Core::GpuTaskGraph m_deferredLightingTaskGraph;
    Core::GpuTaskGraphAnalysis m_deferredLightingTaskGraphAnalysis;
    Core::GpuTaskGraphQueueAssignments m_deferredLightingTaskGraphQueueAssignments;
    // Accepted-route history survives ordinary per-frame graph resets so telemetry can distinguish first, unchanged, and changed assignments.
    // Device/resource invalidation clears the tracker with the artifacts it describes.
    Core::GpuTaskGraphQueueAssignmentTelemetryTracker m_deferredLightingTaskGraphQueueAssignmentTelemetry;
    Core::GpuCompiledGraph m_deferredLightingCompiledGraph;
    Core::GpuRecordedGraph m_deferredLightingRecordedGraph;
    Core::GpuGraphSubmissionTransaction m_deferredLightingSubmissionTransaction;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

