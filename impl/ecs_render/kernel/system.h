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
        Normalize,
        kCount,
    };
    struct ShadowPrepareGraphTask;
    struct MeshViewSetupGraphTask;
    struct MeshViewUploadCommitGraphTask;
    struct SceneShadingSetupGraphTask;
    struct DeferredClearGraphTask;
    struct GbufferGraphTask;
};


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
    friend struct ECSRenderDetail::DeferredClearGraphTask;
    friend struct ECSRenderDetail::GbufferGraphTask;

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
        Core::GpuGraphResourceId currentBindlessSlots,
        Core::GpuGraphResourceId materialContextSlots,
        const Core::GpuGraphResourceId* shadowTraceGeometryResources,
        usize shadowTraceGeometryResourceCount,
        Core::GpuTimingFrameTransaction& frameTimingTransaction,
        Optional<Core::GpuTimingMeasure>& asyncPrefixTiming,
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
        Core::GpuTaskId prefixTask,
        Core::GpuTimingSubmissionTicket& timingTicket
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
        Core::GpuTimingSubmissionTicket& timingTicket
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
        Core::GpuTaskId effectsTask,
        Core::GpuExternalCompletionId surfelCounterReadbackCompletion,
        Core::GpuTimingSubmissionTicket& timingTicket
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
        Core::GpuTimingSubmissionTicket& shadowPrepareTimingTicket,
        Core::GpuTimingSubmissionTicket** graphicsPrefixTimingTickets,
        const bool* asyncPrefixTimingSpansOnePacket,
        Optional<Core::GpuTimingMeasure>& asyncFinalTiming,
        Core::GpuTimingSubmissionTicket& avboitPreTimingTicket,
        Core::GpuTimingSubmissionTicket& avboitDepthWarpTimingTicket,
        Core::GpuTimingSubmissionTicket& avboitExtinctionTimingTicket,
        Core::GpuTimingSubmissionTicket& avboitIntegrationTimingTicket,
        Core::GpuTimingSubmissionTicket& avboitAccumulationTimingTicket,
        Core::GpuTimingSubmissionTicket& shadowVisibilityTimingTicket,
        Core::GpuTimingSubmissionTicket& softwareCausticsTimingTicket,
        Core::GpuTimingSubmissionTicket& surfelGiTimingTicket,
        Core::GpuTimingSubmissionTicket& hardwareCausticsTimingTicket,
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
    // Optional lagged-history selector upload. It must merge into Deferred Lighting's packet, which already owns
    // both history acceptance and the external completion wait for the prior-frame images.
    Core::GpuTaskId m_deferredLaggedLightingHistorySlotsUploadTask;
    Core::GpuTaskId m_deferredShadowPrepareTask;
    Core::GpuTaskId m_graphicsPrefixMeshViewSetupTask;
    Core::GpuTaskId m_graphicsPrefixSceneShadingSetupTask;
    Core::GpuTaskId m_graphicsPrefixDeferredClearTask;
    Core::GpuTaskId m_graphicsPrefixGbufferTask;
    Core::GpuTaskId m_graphicsPrefixTask;
    Core::GpuTaskId m_deferredShadowVisibilityTask;
    Core::GpuTaskId m_deferredSoftwareCausticsTask;
    // Optional initialize/copy prefix for Surfel GI. The final GI task remains the semantic effects endpoint.
    Core::GpuTaskId m_deferredSurfelGiPreparationTask;
    Core::GpuTaskId m_deferredSurfelGiSnapshotCopyTask;
    Core::GpuTaskId m_deferredSurfelGiTask;
    // A rare diagnostic tail: it depends on GI but records/submits after Present on Transfer when available.
    Core::GpuTaskId m_deferredSurfelGiCounterReadbackTask;
    Core::GpuTaskId m_deferredHardwareCausticsTask;
    Core::GpuTaskId m_deferredAvboitPreTask;
    Core::GpuTaskId m_deferredAvboitOccupancyTask;
    Core::GpuTaskId m_deferredAvboitDepthWarpTask;
    // The final immutable extinction upload, when that phase has visible draws. It must live in the native
    // extinction packet so rejected/retried packet recording cannot publish only a partial phase stream.
    Core::GpuTaskId m_deferredAvboitExtinctionStreamTask;
    Core::GpuTaskId m_deferredAvboitExtinctionTask;
    Core::GpuTaskId m_deferredAvboitIntegrationTask;
    // The accumulation phase owns another immutable stream after integration. It must remain in the final
    // accumulation packet so a rejected/retried recording cannot publish only its upload prefix.
    Core::GpuTaskId m_deferredAvboitAccumulationStreamTask;
    Core::GpuTaskId m_deferredAvboitAccumulationTask;
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
    // consumes the visibility result on the same Compute lane, so the result snapshot remains Compute-local.
    Core::CommandListResourceStateHandoff m_shadowComputePersistentStateHandoff;
    Core::CommandListResourceStateHandoff m_shadowVisibilityReturnStateHandoff;
    // Software caustics retain their temporal scratch on the dedicated Compute lane. Hardware dispatch-rays caustics
    // use the Graphics hardware-caustics packet; normal deferred lighting consumes either resolved irradiance on Compute, while
    // the optional lagged path snapshots it for the next Graphics lighting packet.
    Core::CommandListResourceStateHandoff m_causticsComputePersistentStateHandoff;
    Core::CommandListResourceStateHandoff m_causticIrradianceLightingStateHandoff;
    Core::CommandListResourceStateHandoff m_causticIrradianceReturnStateHandoff;
    // Surfel GI is also entirely compute-dispatched, including its RayQuery trace variant. Its field/history stays on
    // AsyncCompute; the resolved full-resolution irradiance is either consumed there or snapshotted for optional
    // frame-lagged Graphics lighting.
    Core::CommandListResourceStateHandoff m_surfelGiComputePersistentStateHandoff;
    // The counter can continue into a late Transfer readback, so retain that exact tail state separately.
    Core::CommandListResourceStateHandoff m_surfelGiCounterPersistentStateHandoff;
    Core::CommandListResourceStateHandoff m_surfelIrradianceReturnStateHandoff;
    bool m_preparedCsgFrameStateValid = false;
    bool m_preparedHasTransparentRenderers = false;
    bool m_preparedShadowVisibilityResourcesValid = false;
    bool m_preparedShadowVisibilityReady = false;
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

