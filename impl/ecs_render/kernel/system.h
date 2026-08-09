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
    void buildShadowPrepareTaskGraph(
        DeferredFrameTargets& deferredTargets,
        Core::GpuTimingSubmissionTicket& timingTicket
    );
    [[nodiscard]] bool declareDeferredGraphicsPrefixTasks(
        DeferredFrameTargets& deferredTargets,
        const CsgFrameState& csgFrameState,
        bool hasOpaqueCsgFrameWork,
        f32 meshViewAspectRatio,
        bool shadowVisibilityExpectedCompute,
        bool surfelGiExpectedCompute,
        Core::GpuGraphResourceId albedo,
        Core::GpuGraphResourceId normal,
        Core::GpuGraphResourceId worldPosition,
        Core::GpuGraphResourceId depth,
        Core::GpuGraphResourceId opaqueColor,
        Core::GpuGraphResourceId currentSurfelIrradiance,
        Core::GpuGraphResourceId sceneShading,
        Core::GpuGraphResourceId lights,
        Core::GpuGraphResourceId meshView,
        Core::GpuTimingFrameTransaction& frameTimingTransaction,
        Optional<Core::GpuTimingMeasure>& asyncPrefixTiming,
        Core::GpuTimingSubmissionTicket& timingTicket
    );
    [[nodiscard]] bool declareDeferredShadowVisibilityTask(
        DeferredFrameTargets& deferredTargets,
        bool shadowVisibilityPrepared,
        bool hardwareShadowSupported,
        Core::GpuGraphResourceId worldPosition,
        Core::GpuGraphResourceId normal,
        Core::GpuGraphResourceId depth,
        Core::GpuGraphResourceId shadowVisibility,
        Core::GpuGraphResourceId currentBindlessSlots,
        Core::GpuGraphResourceId sceneShading,
        Core::GpuGraphResourceId lights,
        Core::GpuGraphResourceId materialContextSlots,
        Core::GpuTaskId prefixTask,
        Core::GpuTimingSubmissionTicket& timingTicket
    );
    [[nodiscard]] bool declareDeferredSoftwareCausticsTask(
        const ECSRenderDetail::GpuTaskGraphFrameScheduleInput& input,
        DeferredFrameTargets& deferredTargets,
        bool shadowVisibilityPrepared,
        Core::GpuGraphResourceId worldPosition,
        Core::GpuGraphResourceId depth,
        Core::GpuGraphResourceId causticIrradiance,
        Core::GpuGraphResourceId currentBindlessSlots,
        Core::GpuGraphResourceId sceneShading,
        Core::GpuGraphResourceId lights,
        Core::GpuGraphResourceId materialContextSlots,
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
        Core::GpuTaskId effectsTask,
        Core::GpuTimingSubmissionTicket& timingTicket
    );
    void buildDeferredLightingTaskGraph(
        const ECSRenderDetail::GpuTaskGraphFrameScheduleInput& input,
        DeferredFrameTargets& deferredTargets,
        const CsgFrameState& csgFrameState,
        bool clearAvboitTargets,
        bool hasTransparentRenderers,
        bool shadowVisibilityPrepared,
        bool hasOpaqueCsgFrameWork,
        f32 meshViewAspectRatio,
        Core::Framebuffer* presentationFramebuffer,
        bool shadowVisibilityExpectedCompute,
        bool surfelGiExpectedCompute,
        Core::GpuTimingFrameTransaction& frameTimingTransaction,
        Optional<Core::GpuTimingMeasure>& asyncPrefixTiming,
        Core::GpuTimingSubmissionTicket& graphicsPrefixTimingTicket,
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
    u16 m_taskGraphDeviceGeneration = 1u;
    // The native Graphics prefix, Shadow Visibility, Software Caustics, Surfel GI, AVBOIT, Hardware Caustics,
    // Deferred Lighting, Composite, Present, optional lagged-history copy, and recovery share one packet graph. The
    // prefix's five command lists remain a temporary recording bridge inside its first Graphics packet.
    Core::GpuTaskGraph m_deferredLightingTaskGraph;
    Core::GpuTaskGraphAnalysis m_deferredLightingTaskGraphAnalysis;
    Core::GpuTaskGraphQueueAssignments m_deferredLightingTaskGraphQueueAssignments;
    Core::GpuCompiledGraph m_deferredLightingCompiledGraph;
    Core::GpuRecordedGraph m_deferredLightingRecordedGraph;
    Core::GpuGraphSubmissionTransaction m_deferredLightingSubmissionTransaction;
    Core::GpuTaskId m_graphicsPrefixMeshViewSetupTask;
    Core::GpuTaskId m_graphicsPrefixSceneShadingSetupTask;
    Core::GpuTaskId m_graphicsPrefixDeferredClearTask;
    Core::GpuTaskId m_graphicsPrefixGbufferTask;
    Core::GpuTaskId m_graphicsPrefixTask;
    Core::GpuTaskId m_deferredShadowVisibilityTask;
    Core::GpuTaskId m_deferredSoftwareCausticsTask;
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
    // Recovery stays unrecorded until a later packet rejects. It uses a generic accepted-producer completion so the
    // Graphics tail can join the latest AsyncCompute/Transfer work, or the Prefix Graphics submission when none did.
    Core::GpuTaskId m_deferredFrameRecoveryTask;
    Core::GpuExternalCompletionId m_deferredLightingHistoryCompletion;
    Core::GpuExternalCompletionId m_deferredFrameRecoveryCompletion;
    bool m_graphicsPrefixMeshViewSetupReady = false;
    bool m_graphicsPrefixSceneShadingSetupReady = false;
    bool m_deferredFrameRecoveryArmed = false;
    bool m_deferredFrameRecoveryRetiresTiming = false;
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

