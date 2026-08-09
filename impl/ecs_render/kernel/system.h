// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/kernel/components.h>
#include <impl/ecs_render/material/material_instance.h>
#include <impl/ecs_render/shared/renderer_state.h>
#include <impl/ecs_render/kernel/subsystems.h>
#include <impl/ecs_render/kernel/task_graph_schedule.h>

#include <core/ecs/system.h>
#include <core/graphics/gpu_timing.h>
#include <core/graphics/render_pass.h>
#include <core/graphics/task_graph/compiler.h>
#include <core/graphics/task_graph/packet_runtime.h>
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
    struct ShadowPrepareGraphTask;
    struct MeshViewSetupGraphTask;
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
    void buildFrameRecoveryTaskGraph(
        Core::GpuTimingFrameTransaction& frameTimingTransaction,
        bool retiresFrameTiming,
        bool waitsForAsyncProducer
    );
    void buildShadowPrepareTaskGraph(
        DeferredFrameTargets& deferredTargets,
        Core::GpuTimingSubmissionTicket& timingTicket
    );
    void buildGraphicsPrefixTaskGraph(
        const ECSRenderDetail::GpuTaskGraphFrameScheduleInput& input,
        DeferredFrameTargets& deferredTargets,
        const CsgFrameState& csgFrameState,
        bool hasOpaqueCsgFrameWork,
        f32 meshViewAspectRatio,
        bool shadowVisibilityRunsOnCompute,
        bool surfelGiRunsOnCompute,
        Core::GpuTimingFrameTransaction& frameTimingTransaction,
        Optional<Core::GpuTimingMeasure>& asyncPrefixTiming,
        Core::GpuTimingSubmissionTicket& timingTicket
    );
    void buildShadowVisibilityTaskGraph(
        const ECSRenderDetail::GpuTaskGraphFrameScheduleInput& input,
        DeferredFrameTargets& deferredTargets,
        bool shadowVisibilityPrepared,
        bool hardwareShadowSupported,
        Core::GpuTimingSubmissionTicket& shadowVisibilityTimingTicket,
        Core::GpuTimingSubmissionTicket& softwareCausticsTimingTicket
    );
    [[nodiscard]] bool declareSoftwareCausticsTask(
        const ECSRenderDetail::GpuTaskGraphFrameScheduleInput& input,
        DeferredFrameTargets& deferredTargets,
        bool shadowVisibilityPrepared,
        const Core::GpuTaskId& shadowVisibilityTask,
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
        Core::GpuExternalCompletionId effectsCompletion,
        Core::GpuTimingSubmissionTicket& timingTicket
    );
    void buildDeferredLightingTaskGraph(
        const ECSRenderDetail::GpuTaskGraphFrameScheduleInput& input,
        DeferredFrameTargets& deferredTargets,
        const CsgFrameState& csgFrameState,
        bool clearAvboitTargets,
        bool hasTransparentRenderers,
        bool shadowVisibilityPrepared,
        Core::Framebuffer* presentationFramebuffer,
        bool shadowVisibilityRunsOnCompute,
        Core::GpuTimingFrameTransaction& frameTimingTransaction,
        Optional<Core::GpuTimingMeasure>& asyncFinalTiming,
        Core::GpuTimingSubmissionTicket& avboitPreTimingTicket,
        Core::GpuTimingSubmissionTicket& avboitDepthWarpTimingTicket,
        Core::GpuTimingSubmissionTicket& avboitExtinctionTimingTicket,
        Core::GpuTimingSubmissionTicket& avboitIntegrationTimingTicket,
        Core::GpuTimingSubmissionTicket& avboitAccumulationTimingTicket,
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
    // Recovery is a graph-owned Graphics packet that retires an accepted frame timing scope after a later packet
    // rejects, optionally waiting for the latest accepted AsyncCompute producer.
    Core::GpuTaskGraph m_frameRecoveryTaskGraph;
    Core::GpuTaskGraphAnalysis m_frameRecoveryTaskGraphAnalysis;
    Core::GpuTaskGraphQueueAssignments m_frameRecoveryTaskGraphQueueAssignments;
    Core::GpuCompiledGraph m_frameRecoveryCompiledGraph;
    Core::GpuRecordedGraph m_frameRecoveryRecordedGraph;
    Core::GpuGraphSubmissionTransaction m_frameRecoverySubmissionTransaction;
    Core::GpuTaskId m_frameRecoveryTask;
    Core::GpuExternalCompletionId m_frameRecoveryAsyncCompletion;
    bool m_frameRecoveryTaskGraphValid = false;
    // Shadow preparation is a graph-owned Graphics packet. Its native final snapshot is the ordered serial base
    // for the following graphics-prefix packet.
    Core::GpuTaskGraph m_shadowPrepareTaskGraph;
    Core::GpuTaskGraphAnalysis m_shadowPrepareTaskGraphAnalysis;
    Core::GpuTaskGraphQueueAssignments m_shadowPrepareTaskGraphQueueAssignments;
    Core::GpuCompiledGraph m_shadowPrepareCompiledGraph;
    Core::GpuRecordedGraph m_shadowPrepareRecordedGraph;
    Core::GpuGraphSubmissionTransaction m_shadowPrepareSubmissionTransaction;
    Core::GpuTaskId m_shadowPrepareTask;
    bool m_shadowPrepareTaskGraphValid = false;
    // The graphics prefix is fully native: its graph owns recording, transport, submission, completion, and
    // lifecycle from mesh-view setup through post-G-buffer normalization.
    Core::GpuTaskGraph m_graphicsPrefixTaskGraph;
    Core::GpuTaskGraphAnalysis m_graphicsPrefixTaskGraphAnalysis;
    Core::GpuTaskGraphQueueAssignments m_graphicsPrefixTaskGraphQueueAssignments;
    Core::GpuCompiledGraph m_graphicsPrefixCompiledGraph;
    Core::GpuRecordedGraph m_graphicsPrefixRecordedGraph;
    Core::GpuGraphSubmissionTransaction m_graphicsPrefixSubmissionTransaction;
    Core::GpuTaskId m_graphicsPrefixMeshViewSetupTask;
    Core::GpuTaskId m_graphicsPrefixSceneShadingSetupTask;
    Core::GpuTaskId m_graphicsPrefixDeferredClearTask;
    Core::GpuTaskId m_graphicsPrefixGbufferTask;
    Core::GpuTaskId m_graphicsPrefixTask;
    u16 m_taskGraphDeviceGeneration = 1u;
    bool m_graphicsPrefixMeshViewSetupReady = false;
    bool m_graphicsPrefixSceneShadingSetupReady = false;
    bool m_graphicsPrefixTaskGraphValid = false;
    // Shadow visibility owns the effects-prefix graph. Software Caustics is present only on the software route;
    // its terminal packet is imported by the deferred graph for Surfel GI rather than stitched through a renderer
    // completion ladder.
    Core::GpuTaskGraph m_shadowVisibilityTaskGraph;
    Core::GpuTaskGraphAnalysis m_shadowVisibilityTaskGraphAnalysis;
    Core::GpuTaskGraphQueueAssignments m_shadowVisibilityTaskGraphQueueAssignments;
    Core::GpuCompiledGraph m_shadowVisibilityCompiledGraph;
    Core::GpuRecordedGraph m_shadowVisibilityRecordedGraph;
    Core::GpuGraphSubmissionTransaction m_shadowVisibilitySubmissionTransaction;
    Core::GpuTaskId m_shadowVisibilityTask;
    Core::GpuTaskId m_softwareCausticsTask;
    Core::GpuExternalCompletionId m_shadowVisibilityPrefixCompletion;
    bool m_shadowVisibilityTaskGraphValid = false;
    // Surfel GI, AVBOIT, Hardware Caustics, Deferred Lighting, Composite, Present, and the optional lagged-history
    // copy share one packet graph. The AVBOIT split topology is selected only when a distinct Compute family exists;
    // otherwise it records as one Graphics packet in the same transaction as its consumers.
    // Hardware and AVBOIT remain staged Graphics producers; live Lighting consumes Hardware through an internal
    // edge, while lagged Lighting reads history and can run independently on the dedicated Compute lane.
    Core::GpuTaskGraph m_deferredLightingTaskGraph;
    Core::GpuTaskGraphAnalysis m_deferredLightingTaskGraphAnalysis;
    Core::GpuTaskGraphQueueAssignments m_deferredLightingTaskGraphQueueAssignments;
    Core::GpuCompiledGraph m_deferredLightingCompiledGraph;
    Core::GpuRecordedGraph m_deferredLightingRecordedGraph;
    Core::GpuGraphSubmissionTransaction m_deferredLightingSubmissionTransaction;
    Core::GpuTaskId m_deferredSurfelGiTask;
    Core::GpuTaskId m_deferredHardwareCausticsTask;
    Core::GpuTaskId m_deferredAvboitPreTask;
    Core::GpuTaskId m_deferredAvboitDepthWarpTask;
    Core::GpuTaskId m_deferredAvboitExtinctionTask;
    Core::GpuTaskId m_deferredAvboitIntegrationTask;
    Core::GpuTaskId m_deferredAvboitAccumulationTask;
    Core::GpuTaskId m_deferredLightingTask;
    Core::GpuTaskId m_deferredCompositeTask;
    Core::GpuTaskId m_deferredPresentTask;
    Core::GpuTaskId m_deferredLaggedLightingHistoryTask;
    Core::GpuExternalCompletionId m_deferredSurfelGiEffectsCompletion;
    Core::GpuExternalCompletionId m_deferredHardwareCausticsPrefixCompletion;
    Core::GpuExternalCompletionId m_deferredLightingPrefixCompletion;
    Core::GpuExternalCompletionId m_deferredLightingHistoryCompletion;
    bool m_deferredLightingTaskGraphValid = false;

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
    Core::CommandListResourceStateHandoff m_surfelIrradianceReturnStateHandoff;
    bool m_preparedCsgFrameStateValid = false;
    bool m_preparedHasTransparentRenderers = false;
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

