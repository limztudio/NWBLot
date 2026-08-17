// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/kernel/components.h>
#include <impl/ecs_render/material/material_instance.h>
#include <impl/ecs_render/shared/renderer_state.h>
#include <impl/ecs_render/kernel/subsystems.h>

#include <core/ecs/system.h>
#include <core/graphics/gpu_timing.h>
#include <core/graphics/render_pass.h>
#include <core/graphics/task_graph/compiler.h>
#include <core/graphics/task_graph/packet_runtime.h>
#include <core/graphics/task_graph/persistent_state.h>
#include <core/graphics/task_graph/presentation_contributor.h>
#include <core/telemetry/frame_graph_contributor.h>
#include <impl/assets/graphics/mesh/binding_slots.h>
#include <impl/assets_material/asset.h>
#include <impl/ecs_csg/frame_state.h>
#include <impl/ecs_csg/shape_registry.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class AssetManager;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Shader;
class Mesh;

namespace ECSRenderDetail{
#if defined(NWB_DEBUG)
    struct MaterialTypedInstanceRangeVector;
#endif
    // Immutable frame facts used while declaring the graph. Queue assignment remains a compiler result; this
    // carries only the features and external-history availability that change which semantic tasks exist.
    struct RendererFrameGraphFeatures{
        bool frameLaggedAsyncLightingEnabled = false;
        bool laggedLightingHistoryReady = false;
        bool laggedLightingHistoryAccepted = false;
        bool hasTransparentRenderers = false;
        bool hardwareCaustics = false;
    };
    // These semantic prefix stages may coalesce into one native submission or split at a compiler-derived
    // cross-queue frontier. Each stage points at a rebindable timing slot so the renderer can attach one ticket
    // to every actual packet after compilation.
    enum class DeferredGraphicsPrefixTimingSlot : u8{
        MeshViewSetup,
        SceneShadingSetup,
        DeferredClear,
        Gbuffer,
        CsgReceiverSpanBuild,
        CsgIntervalCombine,
        CsgIntervalSample,
        Normalize,
        kCount,
    };
    struct ShadowPrepareGraphTask;
    struct MeshViewSetupGraphTask;
    struct MeshViewUploadCommitGraphTask;
    struct SceneShadingSetupGraphTask;
    struct CsgReceiverSpanBuildGraphTask;
    struct CsgIntervalCombineGraphTask;
    struct AvboitCsgReceiverSpanGraphTask;
    struct AvboitCsgIntervalCombineGraphTask;
    struct CsgIntervalSampleGraphTask;
    struct DeferredClearTimingRecordState{
        Core::Graphics* graphics = nullptr;
        Optional<Core::GpuTimingMeasure>* timing = nullptr;
        Core::GpuTimingSubmissionTicket** timingTicket = nullptr;
    };
    // AVBOIT's typed target-clear chain starts/ends one timing scope from its first/last texture clear, preserving
    // the original measurement while the nine values record as individual graph built-ins.
    struct AvboitClearTimingRecordState{
        Core::Graphics* graphics = nullptr;
        Optional<Core::GpuTimingMeasure>* timing = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
    };
    // Opaque prefix timing tickets are rebound after compilation, whereas transparent CSG keeps AVBOIT Pre's
    // stable ticket. The rectangular clear pair resolves either form while preserving one timing range.
    struct CsgIntervalClearTimingRecordState{
        Core::Graphics* graphics = nullptr;
        Optional<Core::GpuTimingMeasure>* timing = nullptr;
        Core::GpuTimingSubmissionTicket** rebindableTimingTicket = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
    };
    struct OpaqueRegularComputeEmulationGraphTask;
    struct OpaqueRegularSharedComputeEmulationGraphTask;
    struct OpaqueCsgReceiverComputeEmulationGraphTask;
    struct OpaqueCsgIntervalSampleComputeEmulationGraphTask;
    struct GbufferGraphTask;
};


namespace __hidden_renderer_task_graph{
    struct AvboitOccupancyComputeEmulationGraphTask;
    struct AvboitOccupancySharedComputeEmulationGraphTask;
    struct AvboitExtinctionComputeEmulationGraphTask;
    struct AvboitExtinctionSharedComputeEmulationGraphTask;
    struct AvboitAccumulationComputeEmulationGraphTask;
    struct AvboitAccumulationSharedComputeEmulationGraphTask;
}


class RendererSystem final : public Core::ECS::ISystem, public Core::IRenderPass, public Core::Telemetry::IFrameGraphContributor{
    template<typename RendererT>
    friend class RendererSystemSubsystemBase;
    friend class RendererShaderSystem;
    friend class RendererMeshSystem;
    friend class RendererMaterialSystem;
    friend class RendererCsgSystem;
    friend class RendererDeferredSystem;
    friend class RendererAvboitSystem;
    friend class RendererRayTracingSystem;
    friend struct ECSRenderDetail::ShadowPrepareGraphTask;
    friend struct ECSRenderDetail::MeshViewSetupGraphTask;
    friend struct ECSRenderDetail::MeshViewUploadCommitGraphTask;
    friend struct ECSRenderDetail::SceneShadingSetupGraphTask;
    friend struct ECSRenderDetail::OpaqueRegularComputeEmulationGraphTask;
    friend struct ECSRenderDetail::OpaqueRegularSharedComputeEmulationGraphTask;
    friend struct ECSRenderDetail::OpaqueCsgReceiverComputeEmulationGraphTask;
    friend struct ECSRenderDetail::OpaqueCsgIntervalSampleComputeEmulationGraphTask;
    friend struct __hidden_renderer_task_graph::AvboitOccupancyComputeEmulationGraphTask;
    friend struct __hidden_renderer_task_graph::AvboitOccupancySharedComputeEmulationGraphTask;
    friend struct __hidden_renderer_task_graph::AvboitExtinctionComputeEmulationGraphTask;
    friend struct __hidden_renderer_task_graph::AvboitExtinctionSharedComputeEmulationGraphTask;
    friend struct __hidden_renderer_task_graph::AvboitAccumulationComputeEmulationGraphTask;
    friend struct __hidden_renderer_task_graph::AvboitAccumulationSharedComputeEmulationGraphTask;
    friend struct ECSRenderDetail::GbufferGraphTask;
    friend struct ECSRenderDetail::CsgReceiverSpanBuildGraphTask;
    friend struct ECSRenderDetail::CsgIntervalCombineGraphTask;
    friend struct ECSRenderDetail::AvboitCsgReceiverSpanGraphTask;
    friend struct ECSRenderDetail::AvboitCsgIntervalCombineGraphTask;
    friend struct ECSRenderDetail::CsgIntervalSampleGraphTask;

private:
    // This is deliberately diagnostic-only: lifecycle ownership remains below in RendererSystem, while the
    // transition-only report lets the opt-in Vulkan smoke prove which accepted-history branch actually ran.
    enum class LaggedLightingReport : u8{
        Unreported,
        NoDedicatedAsyncCompute,
        BootstrapAccepted,
        ActiveHistoryAccepted,
        CurrentFrameAccepted,
    };

public:
    using ShaderPathResolveCallback = RendererShaderPathResolveCallback;


public:
    RendererSystem(
        Core::Alloc::GlobalArena& arena,
        Core::ECS::World& world,
        Core::Graphics& graphics,
        Core::Assets::AssetManager& assetManager,
        ShaderPathResolveCallback shaderPathResolver
    );
    virtual ~RendererSystem()override;


public:
    virtual bool validateResources(u32 width, u32 height, u32 sampleCount)override;
    virtual void invalidateResources()override;

    virtual void update(Core::ECS::World& world, f32 delta)override;

    virtual bool prepareResources(Core::Framebuffer* framebuffer)override;
    virtual void render(Core::Framebuffer* framebuffer)override;
    virtual bool appendFrameGraph(Core::Telemetry::FrameGraphBuilder& builder)override;
    [[nodiscard]] CsgShapeRegistry& csgShapeRegistry(){ return m_csgShapeRegistry; }
    [[nodiscard]] const CsgShapeRegistry& csgShapeRegistry()const{ return m_csgShapeRegistry; }
    // This explicitly trades one frame of shadow/caustic/surfel latency for overlap: Graphics lights the current G-buffer from
    // an accepted prior shadow/caustic/surfel snapshot while AsyncCompute produces the next one. It is off by default
    // and self-bootstraps through the normal current-frame path after every toggle or target recreation.
    void setFrameLaggedAsyncLightingEnabled(const bool enabled)noexcept{
        if(m_frameLaggedAsyncLightingEnabled == enabled)
            return;
        // Preserve a one-shot proof when the opt-in mode is explicitly turned off: the next accepted normal frame
        // confirms that the renderer returned to its established current-frame path instead of merely planning it.
        m_laggedLightingCurrentFrameAcceptancePending = m_frameLaggedAsyncLightingEnabled && !enabled;
        m_frameLaggedAsyncLightingEnabled = enabled;
        m_laggedLightingReport = LaggedLightingReport::Unreported;
        m_laggedLightingReportGeneration = 0u;
        resetLaggedLightingHistoryTracking();
    }
    [[nodiscard]] bool frameLaggedAsyncLightingEnabled()const noexcept{ return m_frameLaggedAsyncLightingEnabled; }
#if !defined(NWB_FINAL) || defined(NWB_ENABLE_TEST_FEATURE_OVERRIDES)
    // Test-only proxy; keep the ray-tracing subsystem itself private to ordinary renderer callers.
    void forceHybridSceneTraversalFallbackForTesting()noexcept;
    void forceHybridSceneTraversalFallbackEveryFrameForTesting()noexcept;
    void forceHybridHardwareFallbackSnapshotStaleForTesting()noexcept;
    // Target-scene A/B seam. Production always uses the graph-owned fold; benchmark arms explicitly select the
    // graph split or the retained monolithic compatibility callback before the first frame is declared.
    void setGraphOwnedSoftTransparentShadowFoldEnabledForTesting(bool enabled)noexcept;
#endif

private:
    [[nodiscard]] bool prepareGpuTimingScopes();
    // These reset groups deliberately remain lifecycle-specific. Some handoffs retain accepted AsyncCompute scratch
    // or producer return state across frames, while unsubmitted work must be discarded before the next recording pass.
    void resetTargetGenerationStateHandoffs()noexcept;
    void resetInvalidatedResourceStateHandoffs()noexcept;
    void resetFrameRecordingStateHandoffs()noexcept;
    void resetAbandonedFrameStateHandoffs()noexcept;
    void resetRejectedShadowVisibilityStateHandoffs()noexcept;
    void invalidateLaggedLightingHistorySubmission()noexcept;
    void resetLaggedLightingHistoryTracking()noexcept;
    [[nodiscard]] bool declareDeferredShadowPrepareTask(
        DeferredFrameTargets& deferredTargets,
        Core::GpuGraphResourceId currentBindlessSlots,
        Core::GpuGraphResourceId materialContextSlots,
        const Core::GpuGraphResourceId* shadowTraceGeometryResources,
        usize shadowTraceGeometryResourceCount,
        const Core::GpuGraphResourceId* softwareBvhBuildStateResources,
        usize softwareBvhBuildStateResourceCount,
        bool softwareTraceResourcesPrepared,
        Core::GpuTimingSubmissionTicket& timingTicket
    );
    [[nodiscard]] bool declareDeferredGraphicsPrefixTasks(
        DeferredFrameTargets& deferredTargets,
        Core::GpuTaskId shadowPrepareTask,
        const CsgFrameState& csgFrameState,
        bool hasOpaqueCsgFrameWork,
        f32 meshViewAspectRatio,
        Core::GpuGraphResourceId albedo,
        Core::GpuGraphResourceId normal,
        Core::GpuGraphResourceId worldPosition,
        Core::GpuGraphResourceId depth,
        Core::GpuGraphResourceId opaqueColor,
        Core::GpuGraphResourceId sceneShading,
        Core::GpuGraphResourceId lights,
        Core::GpuGraphResourceId meshView,
        Core::GpuGraphResourceId materialInstances,
        Core::GpuGraphResourceId materialTyped,
        Core::GpuGraphResourceId csgReceiverRanges,
        Core::GpuGraphResourceId csgCutters,
        Core::GpuGraphResourceId csgClipContextSlots,
        Core::GpuGraphResourceId csgIntervalSampleState,
        Core::GpuGraphResourceId csgCapBackNormal,
        Core::GpuGraphResourceId csgIntervalDepth,
        Core::GpuGraphResourceId csgIntervalId,
        Core::GpuGraphResourceId csgReceiverEventData,
        Core::GpuGraphResourceId csgReceiverEventCount,
        Core::GpuGraphResourceId csgReceiverSpanData,
        Core::GpuGraphResourceId csgReceiverSpanCount,
        Core::GpuGraphResourceId csgRemovedIntervalDepth,
        Core::GpuGraphResourceId csgRemovedIntervalCapNormal,
        Core::GpuGraphResourceId csgRemovedIntervalData,
        Core::GpuGraphResourceId csgRemovedIntervalCount,
        Core::GpuGraphResourceId currentBindlessSlots,
        Core::GpuGraphResourceId materialContextSlots,
        const Core::GpuGraphResourceId* shadowTraceGeometryResources,
        usize shadowTraceGeometryResourceCount,
        Core::GpuGraphResourceSetId shadowTraceGeometrySet,
        Core::GpuTimingFrameTransaction& frameTimingTransaction,
        Optional<Core::GpuTimingMeasure>& asyncPrefixTiming,
        Optional<Core::GpuTimingMeasure>& deferredClearTiming,
        ECSRenderDetail::DeferredClearTimingRecordState& deferredClearTimingState,
        ECSRenderDetail::CsgIntervalClearTimingRecordState& csgIntervalClearTimingState,
        Optional<Core::GpuTimingMeasure>& opaqueRegularSharedComputeEmulationTiming,
        Optional<Core::GpuTimingMeasure>& opaqueCsgIntervalSampleComputeEmulationTiming,
        Core::GpuTimingSubmissionTicket** timingTickets,
        const bool* asyncPrefixTimingSpansOnePacket
    );
    [[nodiscard]] bool declareDeferredShadowVisibilityTask(
        DeferredFrameTargets& deferredTargets,
        bool hardwareShadowSupported,
        Core::GpuGraphResourceId worldPosition,
        Core::GpuGraphResourceId normal,
        Core::GpuGraphResourceId depth,
        Core::GpuGraphResourceId shadowVisibility,
        Core::GpuGraphResourceId currentBindlessSlots,
        Core::GpuGraphResourceId sceneShading,
        Core::GpuGraphResourceId lights,
        Core::GpuGraphResourceId materialContextSlots,
        const Core::GpuGraphResourceId* softwareTraceGeometryResources,
        usize softwareTraceGeometryResourceCount,
        Core::GpuGraphResourceSetId softwareTraceGeometrySet,
        Core::GpuGraphResourceSetId traceMaterialSampledTextureSet,
        Core::GpuTaskId prefixTask,
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
        DeferredFrameTargets& deferredTargets,
        Core::GpuGraphResourceId worldPosition,
        Core::GpuGraphResourceId depth,
        Core::GpuGraphResourceId causticIrradiance,
        Core::GpuGraphResourceId currentBindlessSlots,
        Core::GpuGraphResourceId sceneShading,
        Core::GpuGraphResourceId lights,
        Core::GpuGraphResourceId materialContextSlots,
        const Core::GpuGraphResourceId* softwareTraceGeometryResources,
        usize softwareTraceGeometryResourceCount,
        Core::GpuGraphResourceSetId softwareTraceGeometrySet,
        Core::GpuGraphResourceSetId traceMaterialSampledTextureSet,
        Core::GpuTimingSubmissionTicket& timingTicket,
        Optional<Core::GpuTimingMeasure>& causticPhotonTiming,
        Optional<Core::GpuTimingMeasure>& causticResolveTiming
    );
    [[nodiscard]] bool declareDeferredSurfelGiTask(
        DeferredFrameTargets& deferredTargets,
        Core::GpuGraphResourceId worldPosition,
        Core::GpuGraphResourceId normal,
        Core::GpuGraphResourceId surfelIrradiance,
        Core::GpuGraphResourceId currentBindlessSlots,
        Core::GpuGraphResourceId sceneShading,
        Core::GpuGraphResourceId lights,
        Core::GpuGraphResourceId materialContextSlots,
        const Core::GpuGraphResourceId* traceGeometryResources,
        usize traceGeometryResourceCount,
        Core::GpuGraphResourceSetId traceGeometrySet,
        Core::GpuGraphResourceSetId traceMaterialSampledTextureSet,
        Core::GpuTaskId effectsTask,
        Core::GpuExternalCompletionId surfelCounterReadbackCompletion,
        Core::GpuTimingSubmissionTicket& timingTicket,
        Optional<Core::GpuTimingMeasure>& asyncTiming
    );
    void declareDeferredSurfelCountReadbackTask();
    void buildDeferredLightingTaskGraph(
        const ECSRenderDetail::RendererFrameGraphFeatures& features,
        DeferredFrameTargets& deferredTargets,
        const CsgFrameState& csgFrameState,
        bool clearAvboitTargets,
        bool hasTransparentRenderers,
        bool hasOpaqueCsgFrameWork,
        f32 meshViewAspectRatio,
        Core::Framebuffer* presentationFramebuffer,
        Core::GpuTimingFrameTransaction& frameTimingTransaction,
        Optional<Core::GpuTimingMeasure>& asyncPrefixTiming,
        Optional<Core::GpuTimingMeasure>& deferredClearTiming,
        ECSRenderDetail::DeferredClearTimingRecordState& deferredClearTimingState,
        ECSRenderDetail::CsgIntervalClearTimingRecordState& opaqueCsgIntervalClearTimingState,
        Optional<Core::GpuTimingMeasure>& opaqueRegularSharedComputeEmulationTiming,
        Optional<Core::GpuTimingMeasure>& opaqueCsgIntervalSampleComputeEmulationTiming,
        Core::GpuTimingSubmissionTicket& shadowPrepareTimingTicket,
        Core::GpuTimingSubmissionTicket** graphicsPrefixTimingTickets,
        const bool* asyncPrefixTimingSpansOnePacket,
        Optional<Core::GpuTimingMeasure>& asyncFinalTiming,
        Core::GpuTimingSubmissionTicket& avboitPreTimingTicket,
        ECSRenderDetail::AvboitClearTimingRecordState& avboitClearTimingState,
        ECSRenderDetail::CsgIntervalClearTimingRecordState& transparentCsgIntervalClearTimingState,
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
        Core::GpuTimingSubmissionTicket& presentTimingTicket,
        bool includeLaggedLightingHistoryCapture
    );
    void reportLaggedLightingTransition(LaggedLightingReport report, u64 targetGeneration);
    [[nodiscard]] Core::Alloc::GlobalArena& arena()noexcept{ return m_arena; }
    [[nodiscard]] Core::ECS::World& world()noexcept{ return m_world; }
    [[nodiscard]] Core::Graphics& graphics()noexcept{ return m_graphics; }
    [[nodiscard]] Core::Assets::AssetManager& assetManager()noexcept{ return m_assetManager; }
    [[nodiscard]] ShaderPathResolveCallback& shaderPathResolver()noexcept{ return m_shaderPathResolver; }
    [[nodiscard]] RendererMeshState& meshState()noexcept{ return m_meshState; }
    [[nodiscard]] RendererMaterialState& materialState()noexcept{ return m_materialState; }
    [[nodiscard]] RendererDrawState& drawState()noexcept{ return m_drawState; }
    [[nodiscard]] RendererCsgState& csgState()noexcept{ return m_csgState; }
    [[nodiscard]] RendererDeferredState& deferredState()noexcept{ return m_deferredState; }
    [[nodiscard]] RendererAvboitState& avboitState()noexcept{ return m_avboitState; }
    [[nodiscard]] RendererRayTracingState& rayTracingState()noexcept{ return m_rayTracingState; }
    [[nodiscard]] RendererShaderSystem& shaderSystem()noexcept{ return m_shaderSystem; }
    [[nodiscard]] RendererMeshSystem& meshSystem()noexcept{ return m_meshSystem; }
    [[nodiscard]] RendererMaterialSystem& materialSystem()noexcept{ return m_materialSystem; }
    [[nodiscard]] RendererCsgSystem& csgSystem()noexcept{ return m_csgSystem; }
    [[nodiscard]] RendererAvboitSystem& avboitSystem()noexcept{ return m_avboitSystem; }
    [[nodiscard]] RendererRayTracingSystem& raytracingSystem()noexcept{ return m_raytracingSystem; }

private:
    Core::Alloc::GlobalArena& m_arena;
    Core::ECS::World& m_world;
    Core::Graphics& m_graphics;
    Core::Assets::AssetManager& m_assetManager;
    ShaderPathResolveCallback m_shaderPathResolver;
    CsgShapeRegistry m_csgShapeRegistry;
    // Shadow Preparation, the native Graphics prefix, Shadow Visibility, Software Caustics, Surfel GI, AVBOIT, Hardware Caustics,
    // Deferred Lighting, Composite, Present, optional lagged-history copy, and recovery share one packet graph. The
    // prefix's five command lists remain a temporary recording bridge inside its first Graphics packet.
    Core::GpuTaskGraph m_deferredLightingTaskGraph;
    Core::GpuTaskGraphAnalysis m_deferredLightingTaskGraphAnalysis;
    Core::GpuTaskGraphQueueAssignments m_deferredLightingTaskGraphQueueAssignments;
    Core::GpuCompiledGraph m_deferredLightingCompiledGraph;
    Core::GpuRecordedGraph m_deferredLightingRecordedGraph;
    Core::GpuGraphSubmissionTransaction m_deferredLightingSubmissionTransaction;
    // Optional immutable target-generation selector upload. It must merge into Shadow Preparation's first
    // Graphics packet so its acceptance commits the CPU residency bit atomically with the first consumer.
    Core::GpuTaskId m_deferredBindlessSlotsUploadTask;
    // Immutable ray-trace descriptor-slot snapshot. It must merge into the same first Graphics packet so later
    // Compute trace consumers inherit Shadow Preparation's ConstantBuffer handoff rather than an upload frontier.
    Core::GpuTaskId m_rayTraceMaterialContextSlotsUploadTask;
    // Optional immutable refractive-AABB stream. It must merge into that same first Graphics packet so caustic
    // Compute consumers inherit Shadow Preparation's ShaderResource handoff rather than an upload frontier.
    Core::GpuTaskId m_causticEmissionTargetsUploadTask;
    // Optional per-frame surfel constant payload. It must merge into that same first Graphics packet so the later
    // asynchronous GI pass inherits Shadow Preparation's ConstantBuffer handoff rather than an upload frontier.
    Core::GpuTaskId m_surfelFrameConstantsUploadTask;
    // Optional ABI-coupled shadow material context batch. Every upload must merge into Shadow Preparation so its SRV
    // handoff, rather than any individual upload, owns the later asynchronous trace consumers.
    Core::GpuTaskId m_shadowInstanceMaterialUploadTask;
    Core::GpuTaskId m_shadowInstanceUploadTask;
    Core::GpuTaskId m_shadowMaterialTypedUploadTask;
    // Optional CPU-built software scene-BVH pair. Nodes address the companion leaf-instance stream, so both must
    // merge into Shadow Preparation's packet before it owns the later asynchronous ShaderResource handoff.
    Core::GpuTaskId m_sceneBvhNodesUploadTask;
    Core::GpuTaskId m_sceneBvhInstancesUploadTask;
    // Optional lagged-history selector upload. It must merge into Deferred Lighting's packet, which already owns
    // both history acceptance and the external completion wait for the prior-frame images.
    Core::GpuTaskId m_deferredLaggedLightingHistorySlotsUploadTask;
    Core::GpuTaskId m_deferredShadowPrepareTask;
    // Pure-software prepared per-mesh builds lower their typed sentinel clears and native compute callbacks before
    // Shadow Preparation's existing scene-build/acceptance endpoint. Both bounds must remain in that same packet.
    Core::GpuTaskId m_deferredShadowPrepareSoftwareBvhBuildFirstTask;
    Core::GpuTaskId m_deferredShadowPrepareSoftwareBvhBuildLastTask;
    // Hybrid HW-to-SW preparation retains its opaque fallback inside the accepting Shadow Preparation packet, but
    // records the software continuation as a separate packet-local callback so its bridge can be lowered next.
    Core::GpuTaskId m_deferredShadowPrepareHybridSoftwareTailTask;
    // Prepared TLAS/BLAS builds record in Shadow Preparation, while this adjacent state-only callback publishes
    // their descriptor-visible AccelStructRead boundaries. It must remain in the same first Graphics packet.
    Core::GpuTaskId m_deferredShadowPrepareAccelStructFinalizeTask;
    Core::GpuTaskId m_graphicsPrefixMeshViewSetupTask;
    Core::GpuTaskId m_graphicsPrefixSceneShadingSetupTask;
    // The timer begins inside this first built-in clear and ends inside the terminal opaque-color clear below.
    // Both IDs must compile into one Graphics packet before recording.
    Core::GpuTaskId m_graphicsPrefixDeferredClearFirstTask;
    Core::GpuTaskId m_graphicsPrefixDeferredClearTask;
    // The opaque CSG work-region clear is a two-value typed rectangle chain. Both tasks must remain in one
    // Graphics packet so its first/last hooks retain the existing CSG-clear timing interval.
    Core::GpuTaskId m_graphicsPrefixCsgIntervalClearFirstTask;
    Core::GpuTaskId m_graphicsPrefixCsgIntervalClearTask;
    // Pairwise-distinct opaque regular compute-emulation outputs dispatch before G-buffer rasterization. This
    // producer must share G-buffer's existing primary-Graphics packet for the graph-owned UAV-to-VertexBuffer
    // handoff to remain inside the semantic prefix range.
    Core::GpuTaskId m_graphicsPrefixOpaqueComputeEmulationTask;
    // Small shared-output regular paths keep dispatch/raster alternation in the same packet. Retain every phase ID
    // so runtime validation can prove the strict D(A) -> R(A) -> ... packet order, rather than merely proving
    // that the two endpoint callbacks coalesced. The active prefix holds four, six, or eight phases for two, three,
    // or four draws.
    Core::GpuTaskId m_graphicsPrefixOpaqueSharedComputeEmulationTasks[8u] = {};
    usize m_graphicsPrefixOpaqueSharedComputeEmulationTaskCount = 0u;
    // Receiver-surface CSG has its own readiness gate but needs the same packet-local output handoff.
    Core::GpuTaskId m_graphicsPrefixOpaqueCsgReceiverComputeEmulationTask;
    // Interval-sample CSG follows Combine and has a separate alias-free output plan. Its raster consumer remains
    // the existing CSG Interval Sample task, so both IDs must stay in that primary Graphics packet.
    Core::GpuTaskId m_graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationTask;
    Core::GpuTaskId m_graphicsPrefixGbufferTask;
    Core::GpuTaskId m_graphicsPrefixCsgReceiverSpanTask;
    Core::GpuTaskId m_graphicsPrefixCsgIntervalCombineTask;
    Core::GpuTaskId m_graphicsPrefixCsgIntervalSampleTask;
    Core::GpuTaskId m_graphicsPrefixTask;
    // Prepared soft-transparent shadow frames split opaque production, first wavelet, resolve tail, transparent
    // trace, optional temporal merge, transparent first wavelet, and terminal resolve tail. Every active task must
    // compile into one packet; the terminal ID remains the output/acceptance/recovery owner.
    Core::GpuTaskId m_deferredShadowVisibilityOpaqueTask;
    Core::GpuTaskId m_deferredShadowVisibilityOpaqueFirstWaveletTask;
    Core::GpuTaskId m_deferredShadowVisibilityOpaqueResolveTask;
    Core::GpuTaskId m_deferredShadowVisibilityTransparentTraceTask;
    Core::GpuTaskId m_deferredShadowVisibilityTransparentTemporalMergeTask;
    Core::GpuTaskId m_deferredShadowVisibilityTransparentFirstWaveletTask;
    // Adaptive software-shadow scratch work is graph-declared around the retained monolithic visibility callback.
    // Every valid ID must share that callback's semantic packet so timing, recovery, and CPU readback acceptance
    // retain the original single Shadow Visibility endpoint.
    Core::GpuTaskId m_deferredShadowVisibilityAdaptiveStatsClearTask;
    Core::GpuTaskId m_deferredShadowVisibilityAdaptiveCounterClearTask;
    Core::GpuTaskId m_deferredShadowVisibilityAdaptiveStatsReadbackTask;
    // The retained monolithic route always clears visibility to all-lit immediately before its callback. The clear
    // must share that semantic packet so its CopyDest -> UAV handoff and the existing acceptance endpoint stay
    // graph-owned.
    Core::GpuTaskId m_deferredShadowVisibilityAllLitClearTask;
    Core::GpuTaskId m_deferredShadowVisibilityTask;
    Core::GpuTaskId m_deferredSoftwareCausticsTask;
    // Both hardware and software caustics use this typed black-output clear. The selected producer must share its
    // packet so the established effects timing and acceptance endpoint remains unchanged.
    Core::GpuTaskId m_deferredCausticIrradianceClearTask;
    // The temporal accumulator bootstrap is conditional, but when present it must remain in the same packet as the
    // selected caustic producer; that producer commits initialization only on acceptance.
    Core::GpuTaskId m_deferredCausticAccumulatorBootstrapClearTask;
    // Non-temporal caustics reset the accumulator every frame through the selected producer packet; the producer
    // commits the matching CPU reset only after that packet accepts.
    Core::GpuTaskId m_deferredCausticAccumulatorNonTemporalClearTask;
    // A warm temporal accumulator decays in a mergeable graph task before its selected photon producer.
    Core::GpuTaskId m_deferredCausticAccumulatorDecayTask;
    // Photon, geometry downsample, resolve prepare, five wavelets, upsample, and timing close are distinct graph
    // tasks, but must remain in the selected caustics packet so the compiler owns immutable and every ping-pong
    // handoff without changing the effects endpoint.
    Core::GpuTaskId m_deferredCausticPhotonTask;
    Core::GpuTaskId m_deferredCausticGeometryTask;
    Core::GpuTaskId m_deferredCausticResolvePrepareTask;
    Core::GpuTaskId m_deferredCausticResolveWaveletTask;
    Core::GpuTaskId m_deferredCausticResolveSecondWaveletTask;
    Core::GpuTaskId m_deferredCausticResolveThirdWaveletTask;
    Core::GpuTaskId m_deferredCausticResolveFourthWaveletTask;
    Core::GpuTaskId m_deferredCausticResolveFifthWaveletTask;
    Core::GpuTaskId m_deferredCausticResolveUpsampleTask;
    bool m_deferredCausticProducerDispatched = false;
    // The typed output clear plus optional persistent-initialization clear chain/copy prefix form Surfel GI's
    // graph-owned setup. The lifecycle task publishes the initialization only after all four typed clears in its
    // packet accept. Age/free, the per-frame cell-head clear, hash build, Spawn, trace-build-args, trace, resolve,
    // and upsample work stay in the same semantic effects packet so the compiler owns each handoff without changing
    // the effects endpoint.
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
    // The normal graph path keeps its serial first/last typed CopyDest clear tasks in AVBOIT Pre's timed Graphics
    // packet. Their pair proves the whole nine-clear chain stays at the established semantic endpoint.
    Core::GpuTaskId m_deferredAvboitClearFirstTask;
    Core::GpuTaskId m_deferredAvboitClearTask;
    // Prepared transparent CSG repeats the same two-value work-region clear before its interval producer.
    Core::GpuTaskId m_deferredAvboitTransparentCsgIntervalClearFirstTask;
    Core::GpuTaskId m_deferredAvboitTransparentCsgIntervalClearTask;
    Core::GpuTaskId m_deferredAvboitPreTask;
    // Prepared transparent CSG split: receiver-span and interval-combine retain the AVBOIT-pre packet so phase-
    // local occupancy uploads cannot overwrite the frozen CSG stream before those callbacks record.
    Core::GpuTaskId m_deferredAvboitCsgReceiverSpanTask;
    Core::GpuTaskId m_deferredAvboitCsgIntervalCombineTask;
    // The final immutable Occupancy upload and optional graph-owned regular producer/two-, three-, or four-draw
    // alternating shared-output phases stay in AVBOIT Pre's accepting Graphics packet before its later Depth-Warp
    // Compute dependency.
    Core::GpuTaskId m_deferredAvboitOccupancyStreamTask;
    Core::GpuTaskId m_deferredAvboitOccupancyComputeEmulationTask;
    Core::GpuTaskId m_deferredAvboitOccupancySharedComputeEmulationTasks[8u] = {};
    usize m_deferredAvboitOccupancySharedComputeEmulationTaskCount = 0u;
    Core::GpuTaskId m_deferredAvboitOccupancyTask;
    Core::GpuTaskId m_deferredAvboitDepthWarpTask;
    // The optional alias-free regular/CSG producer or narrowly retained two-, three-, or four-draw shared-output
    // phases must remain in Extinction's selected Graphics packet so their material timing interval and
    // generated-vertex handoff share the consumer's token.
    Core::GpuTaskId m_deferredAvboitExtinctionComputeEmulationTask;
    Core::GpuTaskId m_deferredAvboitExtinctionSharedComputeEmulationTasks[8u] = {};
    usize m_deferredAvboitExtinctionSharedComputeEmulationTaskCount = 0u;
    // The final immutable extinction upload, when that phase has visible draws. It must live in the native
    // extinction packet so rejected/retried packet recording cannot publish only a partial phase stream.
    Core::GpuTaskId m_deferredAvboitExtinctionStreamTask;
    Core::GpuTaskId m_deferredAvboitExtinctionTask;
    Core::GpuTaskId m_deferredAvboitIntegrationTask;
    // The accumulation phase owns another immutable stream after integration. It must remain in the final
    // accumulation packet so a rejected/retried recording cannot publish only its upload prefix.
    Core::GpuTaskId m_deferredAvboitAccumulationStreamTask;
    // The optional alias-free regular or CSG-only producer stays immediately before Accumulation so the
    // generated-vertex handoff and its cross-callback timing interval share the terminal Graphics packet and
    // finalizer.
    Core::GpuTaskId m_deferredAvboitAccumulationComputeEmulationTask;
    // A narrowly accepted two-, three-, or four-draw shared-output regular stream retains every alternating D/R
    // phase so the runtime can prove exact packet order instead of accepting only its two endpoints.
    Core::GpuTaskId m_deferredAvboitAccumulationSharedComputeEmulationTasks[8u] = {};
    usize m_deferredAvboitAccumulationSharedComputeEmulationTaskCount = 0u;
    Core::GpuTaskId m_deferredAvboitAccumulationTask;
    // A no-op Graphics task that returns accumulation color outputs and read-only deferred depth to sampled state.
    Core::GpuTaskId m_deferredAvboitAccumulationFinalizeTask;
    Core::GpuTaskId m_deferredLightingTask;
    Core::GpuTaskId m_deferredCompositeTask;
    // Optional final overlay appended by a registered presentation contributor. The deferred scene output remains
    // separately named because lagged-history copies may begin from it while the overlay finishes on Graphics.
    Core::GpuTaskId m_deferredPresentationOverlayTask;
    Core::GpuTaskId m_deferredPresentTask;
    Core::GpuTaskId m_deferredLaggedLightingHistoryTask;
    // Recovery stays unrecorded until a later packet rejects. Its graph-owned submission join waits for every latest
    // accepted non-Graphics physical queue while Graphics queue order covers the accepted prefix.
    Core::GpuTaskId m_deferredFrameRecoveryTask;
    // Imported only while the preceding frame's diagnostic Transfer readback remains in flight.
    Core::GpuExternalCompletionId m_deferredSurfelGiCounterReadbackCompletion;
    Core::GpuExternalCompletionId m_deferredLightingHistoryCompletion;
    bool m_graphicsPrefixMeshViewSetupReady = false;
    bool m_graphicsPrefixSceneShadingSetupReady = false;
    bool m_deferredFrameRecoveryArmed = false;
    bool m_deferredFrameRecoveryRetiresTiming = false;
    bool m_deferredLightingTaskGraphValid = false;
    Core::IGpuTaskGraphPresentationContributor* m_preparedTaskGraphPresentationContributor = nullptr;
    bool m_deferredPresentationOverlayRequired = false;

private:
    RendererMeshState m_meshState;
    RendererMaterialState m_materialState;
    RendererDrawState m_drawState;
    RendererCsgState m_csgState;
    RendererDeferredState m_deferredState;
    RendererAvboitState m_avboitState;
    RendererRayTracingState m_rayTracingState;
    CsgFrameState m_preparedCsgFrameState;
    // Compute-only shadow scratch/history retains accepted graph packet state across frames. Deferred lighting now
    // consumes the visibility result on the same Compute lane, so retain the private scratch and its typed backings.
    Core::GpuPersistentResourceStateCache m_shadowComputePersistentState;
    Core::GpuPersistentResourceStateCache m_shadowVisibilityReturnState;
    // Native TLAS/BLAS, software-BVH build storage, and normalized trace geometry change inside Shadow Preparation.
    // Retain only accepted live generations so the next frame's first graph packet seeds their real acceleration,
    // UAV, and descriptor-visible ShaderResource states.
    Core::GpuPersistentResourceStateCache m_shadowPreparePersistentState;
    // Software caustics retain their temporal scratch on the dedicated Compute lane. Hardware dispatch-rays caustics
    // use the Graphics hardware-caustics packet; normal deferred lighting consumes either resolved irradiance on Compute, while
    // the optional lagged path snapshots it for the next Graphics lighting packet.
    Core::GpuPersistentResourceStateCache m_causticsComputePersistentState;
    Core::GpuPersistentResourceStateCache m_causticIrradianceLightingState;
    Core::GpuPersistentResourceStateCache m_causticIrradianceReturnState;
    // Surfel GI is also entirely compute-dispatched, including its RayQuery trace variant. Its field/history stays on
    // AsyncCompute; the resolved full-resolution irradiance is either consumed there or snapshotted for optional
    // frame-lagged Graphics lighting. Retain only the accepted private Compute scratch and its typed backings.
    Core::GpuPersistentResourceStateCache m_surfelGiComputePersistentState;
    // The counter can continue into a late Transfer readback, so retain the accepted tail state and its typed backing
    // separately. The next Surfel-GI packet imports this cache through its semantic task binding.
    Core::GpuPersistentResourceStateCache m_surfelGiCounterPersistentState;
    Core::GpuPersistentResourceStateCache m_surfelIrradianceReturnState;
    bool m_preparedCsgFrameStateValid = false;
    bool m_preparedHasTransparentRenderers = false;
    bool m_preparedShadowVisibilityResourcesValid = false;
    bool m_preparedShadowVisibilityReady = false;
#if !defined(NWB_FINAL) || defined(NWB_ENABLE_TEST_FEATURE_OVERRIDES)
    bool m_graphOwnedSoftTransparentShadowFoldEnabledForTesting = true;
    bool m_graphOwnedSoftTransparentShadowFoldBenchmarkForTesting = false;
    bool m_reportedGraphOwnedSoftTransparentShadowFoldBenchmarkForTesting = false;
#endif
    bool m_frameLaggedAsyncLightingEnabled = false;
    LaggedLightingReport m_laggedLightingReport = LaggedLightingReport::Unreported;
    u64 m_laggedLightingReportGeneration = 0u;
    bool m_laggedLightingCurrentFrameAcceptancePending = false;
    Core::QueueSubmissionToken m_laggedLightingHistorySubmissionToken;
    // Deferred target creation increments this identity for every target generation. It prevents a recycled descriptor
    // slot or allocator address from making a freshly recreated history look accepted.
    u64 m_laggedLightingHistoryGeneration = 0u;
    // A partially accepted frame whose recovery packet cannot be submitted is not recoverable by guessing. End this
    // device generation and rebuild resources before rendering resumes.
    bool m_frameRenderRecoveryFailed = false;

private:
    RendererShaderSystem m_shaderSystem;
    RendererMeshSystem m_meshSystem;
    RendererMaterialSystem m_materialSystem;
    RendererCsgSystem m_csgSystem;
    RendererDeferredSystem m_deferredSystem;
    RendererAvboitSystem m_avboitSystem;
    RendererRayTracingSystem m_raytracingSystem;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

