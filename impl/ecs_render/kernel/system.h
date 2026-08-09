// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/kernel/components.h>
#include <impl/ecs_render/material/material_instance.h>
#include <impl/ecs_render/shared/renderer_state.h>
#include <impl/ecs_render/kernel/subsystems.h>
#include <impl/ecs_render/kernel/task_graph_schedule.h>

#include <core/ecs/system.h>
#include <core/graphics/render_pass.h>
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
    class FrameExecutionPlan;
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
        resetLaggedLightingHistoryCopyStateHandoffs();
    }
    [[nodiscard]] bool frameLaggedAsyncLightingEnabled()const noexcept{ return m_frameLaggedAsyncLightingEnabled; }

private:
    [[nodiscard]] bool ensureFrameCommandLists();
    [[nodiscard]] bool prepareGpuTimingScopes();
    [[nodiscard]] bool recordShadowPrepareCommandList(DeferredFrameTargets& deferredTargets);
    // These reset groups deliberately remain lifecycle-specific. Some handoffs retain accepted AsyncCompute scratch
    // or producer return state across frames, while unsubmitted work must be discarded before the next recording pass.
    void resetTargetGenerationStateHandoffs()noexcept;
    void resetInvalidatedResourceStateHandoffs()noexcept;
    void resetFrameRecordingStateHandoffs()noexcept;
    void resetAbandonedFrameStateHandoffs()noexcept;
    void resetRejectedShadowVisibilityStateHandoffs()noexcept;
    void resetLaggedLightingHistoryCopyStateHandoffs()noexcept;
    void invalidateLaggedLightingHistorySubmission()noexcept;
    void resetLaggedLightingHistoryTracking()noexcept;
    void buildGpuTaskGraph(
        const ECSRenderDetail::GpuTaskGraphFrameScheduleInput& input,
        const DeferredFrameTargets& deferredTargets,
        const ECSRenderDetail::FrameExecutionPlan& legacyPlan
    );
    void buildLaggedLightingHistoryTaskGraph(
        const ECSRenderDetail::GpuTaskGraphFrameScheduleInput& input,
        const DeferredFrameTargets& deferredTargets
    );
    void buildDeferredCompositeTaskGraph(
        const ECSRenderDetail::GpuTaskGraphFrameScheduleInput& input,
        DeferredFrameTargets& deferredTargets,
        Core::GpuTimingSubmissionTicket& timingTicket
    );
    void buildShadowVisibilityTaskGraph(
        const ECSRenderDetail::GpuTaskGraphFrameScheduleInput& input,
        DeferredFrameTargets& deferredTargets,
        bool shadowVisibilityPrepared,
        bool hardwareShadowSupported,
        Core::GpuTimingSubmissionTicket& timingTicket
    );
    void buildSoftwareCausticsTaskGraph(
        const ECSRenderDetail::GpuTaskGraphFrameScheduleInput& input,
        DeferredFrameTargets& deferredTargets,
        bool shadowVisibilityPrepared,
        Core::GpuTimingSubmissionTicket& timingTicket
    );
    void buildSurfelGiTaskGraph(
        const ECSRenderDetail::GpuTaskGraphFrameScheduleInput& input,
        DeferredFrameTargets& deferredTargets,
        Core::GpuTimingSubmissionTicket& timingTicket
    );
    void buildDeferredLightingTaskGraph(
        const ECSRenderDetail::GpuTaskGraphFrameScheduleInput& input,
        DeferredFrameTargets& deferredTargets,
        Core::GpuTimingSubmissionTicket& timingTicket
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
    // The semantic sidecar remains the parity oracle for still-legacy packet work. Migrated tasks own independent
    // graphs and submissions; FrameGraph remains observational telemetry only.
    Core::GpuTaskGraph m_gpuTaskGraph;
    Core::GpuTaskGraphAnalysis m_gpuTaskGraphAnalysis;
    Core::GpuTaskGraphQueueAssignments m_gpuTaskGraphQueueAssignments;
    Core::GraphicsVector<Core::GpuTaskId> m_gpuTaskGraphWorkTasks;
    Core::GraphicsVector<Core::GpuTaskDependencyEdge> m_gpuTaskGraphLegacyMismatches;
    Core::GraphicsVector<Core::GpuTaskId> m_gpuTaskGraphLegacyQueueMismatches;
    u16 m_gpuTaskGraphDeviceGeneration = 1u;
    bool m_gpuTaskGraphValid = false;
    // Live graph tasks are intentionally isolated from the telemetry sidecar. Each owns its packet submission while
    // the remaining renderer work continues through the legacy plan until it is individually migrated.
    Core::GpuTaskGraph m_laggedLightingHistoryTaskGraph;
    Core::GpuTaskGraphAnalysis m_laggedLightingHistoryTaskGraphAnalysis;
    Core::GpuTaskGraphQueueAssignments m_laggedLightingHistoryTaskGraphQueueAssignments;
    Core::GpuCompiledGraph m_laggedLightingHistoryCompiledGraph;
    Core::GpuRecordedGraph m_laggedLightingHistoryRecordedGraph;
    Core::GpuGraphSubmissionTransaction m_laggedLightingHistorySubmissionTransaction;
    Core::GpuTaskId m_laggedLightingHistoryTask;
    Core::GpuExternalCompletionId m_laggedLightingPresentationCompletion;
    bool m_laggedLightingHistoryTaskGraphValid = false;
    // Deferred composite is the first renderer pass migrated after the built-in copy pilot. It retains only the
    // existing state-handoff bridge until compiler-generated barriers supersede it.
    Core::GpuTaskGraph m_deferredCompositeTaskGraph;
    Core::GpuTaskGraphAnalysis m_deferredCompositeTaskGraphAnalysis;
    Core::GpuTaskGraphQueueAssignments m_deferredCompositeTaskGraphQueueAssignments;
    Core::GpuCompiledGraph m_deferredCompositeCompiledGraph;
    Core::GpuRecordedGraph m_deferredCompositeRecordedGraph;
    Core::GpuGraphSubmissionTransaction m_deferredCompositeSubmissionTransaction;
    Core::GpuTaskId m_deferredCompositeTask;
    Core::GpuExternalCompletionId m_deferredCompositeLightingCompletion;
    bool m_deferredCompositeTaskGraphValid = false;
    // Shadow visibility owns its graph task, physical queue, and submission completion.  The manual state handoff
    // remains only until automatic graph barriers replace this migration bridge.
    Core::GpuTaskGraph m_shadowVisibilityTaskGraph;
    Core::GpuTaskGraphAnalysis m_shadowVisibilityTaskGraphAnalysis;
    Core::GpuTaskGraphQueueAssignments m_shadowVisibilityTaskGraphQueueAssignments;
    Core::GpuCompiledGraph m_shadowVisibilityCompiledGraph;
    Core::GpuRecordedGraph m_shadowVisibilityRecordedGraph;
    Core::GpuGraphSubmissionTransaction m_shadowVisibilitySubmissionTransaction;
    Core::GpuTaskId m_shadowVisibilityTask;
    Core::GpuExternalCompletionId m_shadowVisibilityPrefixCompletion;
    bool m_shadowVisibilityTaskGraphValid = false;
    // Software caustics own their graph task and native packet. The manual state handoff remains only until the
    // automatic-barrier phase; the hardware dispatch-rays variant retains its separate legacy migration step.
    Core::GpuTaskGraph m_softwareCausticsTaskGraph;
    Core::GpuTaskGraphAnalysis m_softwareCausticsTaskGraphAnalysis;
    Core::GpuTaskGraphQueueAssignments m_softwareCausticsTaskGraphQueueAssignments;
    Core::GpuCompiledGraph m_softwareCausticsCompiledGraph;
    Core::GpuRecordedGraph m_softwareCausticsRecordedGraph;
    Core::GpuGraphSubmissionTransaction m_softwareCausticsSubmissionTransaction;
    Core::GpuTaskId m_softwareCausticsTask;
    Core::GpuExternalCompletionId m_softwareCausticsShadowVisibilityCompletion;
    bool m_softwareCausticsTaskGraphValid = false;
    // Surfel GI is a coarse graph-owned compute task. Its manual state handoffs remain until the barrier phase,
    // while its producer and consumer completion edges are already graph submissions.
    Core::GpuTaskGraph m_surfelGiTaskGraph;
    Core::GpuTaskGraphAnalysis m_surfelGiTaskGraphAnalysis;
    Core::GpuTaskGraphQueueAssignments m_surfelGiTaskGraphQueueAssignments;
    Core::GpuCompiledGraph m_surfelGiCompiledGraph;
    Core::GpuRecordedGraph m_surfelGiRecordedGraph;
    Core::GpuGraphSubmissionTransaction m_surfelGiSubmissionTransaction;
    Core::GpuTaskId m_surfelGiTask;
    Core::GpuExternalCompletionId m_surfelGiEffectsCompletion;
    bool m_surfelGiTaskGraphValid = false;
    // Deferred lighting owns its task, native recording, and two explicit completion imports. The manual state
    // bridge remains only until the compiler barrier phase replaces it.
    Core::GpuTaskGraph m_deferredLightingTaskGraph;
    Core::GpuTaskGraphAnalysis m_deferredLightingTaskGraphAnalysis;
    Core::GpuTaskGraphQueueAssignments m_deferredLightingTaskGraphQueueAssignments;
    Core::GpuCompiledGraph m_deferredLightingCompiledGraph;
    Core::GpuRecordedGraph m_deferredLightingRecordedGraph;
    Core::GpuGraphSubmissionTransaction m_deferredLightingSubmissionTransaction;
    Core::GpuTaskId m_deferredLightingTask;
    Core::GpuExternalCompletionId m_deferredLightingGraphicsEffectsCompletion;
    Core::GpuExternalCompletionId m_deferredLightingSurfelGiCompletion;
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
    Core::CommandListResourceStateHandoff m_shadowPrepareStateHandoff;
    Core::CommandListResourceStateHandoff m_meshViewSetupStateHandoff;
    Core::CommandListResourceStateHandoff m_sceneShadingSetupStateHandoff;
    Core::CommandListResourceStateHandoff m_deferredClearStateHandoff;
    Core::CommandListResourceStateHandoff m_frameSetupStateFanInHandoff;
    Core::CommandListResourceStateHandoff m_gbufferStateHandoff;
    Core::CommandListResourceStateHandoff m_postGbufferNormalizedStateHandoff;
    Core::CommandListResourceStateHandoff m_shadowComputeBaseStateHandoff;
    Core::CommandListResourceStateHandoff m_shadowComputeInputStateHandoff;
    // Compute-only shadow scratch/history retains its state across frames. Deferred lighting now consumes the
    // visibility result on the same Compute lane, so the result snapshot remains Compute-local until the next frame.
    Core::CommandListResourceStateHandoff m_shadowComputePersistentStateHandoff;
    Core::CommandListResourceStateHandoff m_shadowVisibilityStateHandoff;
    Core::CommandListResourceStateHandoff m_shadowVisibilityLightingStateHandoff;
    Core::CommandListResourceStateHandoff m_shadowVisibilityReturnStateHandoff;
    // Software caustics retain their temporal scratch on the dedicated Compute lane. Hardware dispatch-rays caustics
    // use the Graphics hardware-caustics packet; normal deferred lighting consumes either resolved irradiance on Compute, while
    // the optional lagged path snapshots it for the next Graphics lighting packet.
    Core::CommandListResourceStateHandoff m_causticsComputeBaseStateHandoff;
    Core::CommandListResourceStateHandoff m_causticsComputeInputStateHandoff;
    Core::CommandListResourceStateHandoff m_causticsComputePersistentStateHandoff;
    Core::CommandListResourceStateHandoff m_causticsStateHandoff;
    Core::CommandListResourceStateHandoff m_causticIrradianceLightingStateHandoff;
    Core::CommandListResourceStateHandoff m_causticIrradianceReturnStateHandoff;
    // Surfel GI is also entirely compute-dispatched, including its RayQuery trace variant. Its field/history stays on
    // AsyncCompute; the resolved full-resolution irradiance is either consumed there or snapshotted for optional
    // frame-lagged Graphics lighting.
    Core::CommandListResourceStateHandoff m_surfelGiComputeBaseStateHandoff;
    Core::CommandListResourceStateHandoff m_surfelGiComputeInputStateHandoff;
    Core::CommandListResourceStateHandoff m_surfelGiComputePersistentStateHandoff;
    Core::CommandListResourceStateHandoff m_surfelGiStateHandoff;
    Core::CommandListResourceStateHandoff m_surfelIrradianceLightingStateHandoff;
    Core::CommandListResourceStateHandoff m_surfelIrradianceReturnStateHandoff;
    // Normal lighting runs on AsyncCompute after the Graphics AVBOIT chain. The opt-in lagged path instead runs it on
    // Graphics with accepted history, so its narrow input handoff contains only current G-buffer data, AVBOIT state,
    // and the reusable opaque-color storage output.
    Core::CommandListResourceStateHandoff m_deferredLightingBaseStateHandoff;
    Core::CommandListResourceStateHandoff m_deferredLightingInputStateHandoff;
    Core::CommandListResourceStateHandoff m_deferredLightingStateHandoff;
    Core::CommandListResourceStateHandoff m_avboitLightingStateHandoff;
    Core::CommandListResourceStateHandoff m_avboitCompositeStateHandoff;
    Core::CommandListResourceStateHandoff m_opaqueColorCompositeStateHandoff;
    Core::CommandListResourceStateHandoff m_deferredCompositeBaseStateHandoff;
    Core::CommandListResourceStateHandoff m_deferredCompositeInputStateHandoff;
    Core::CommandListResourceStateHandoff m_deferredCompositeStateHandoff;
    Core::CommandListResourceStateHandoff m_compositeColorPresentStateHandoff;
    Core::CommandListResourceStateHandoff m_deferredPresentBaseStateHandoff;
    Core::CommandListResourceStateHandoff m_deferredPresentInputStateHandoff;
    Core::CommandListResourceStateHandoff m_deferredPresentStateHandoff;
    // The source images leave the accepted producer/current-lighting path in this state while the graph-owned copy
    // records into separately tracked history. The history textures themselves restore to Common on close.
    Core::CommandListResourceStateHandoff m_laggedLightingHistoryCopyInputStateHandoff;
    Core::CommandListResourceStateHandoff m_laggedLightingHistoryCopyStateHandoff;
    // AVBOIT's raster occupancy/extinction/accumulation stages stay on Graphics, while the depth-warp and integration
    // dispatches run on AsyncCompute. All inter-stage work resources use concurrent sharing; these handoffs carry
    // state only and are submitted in strict Graphics -> Compute -> Graphics order.
    Core::CommandListResourceStateHandoff m_avboitPreStateHandoff;
    Core::CommandListResourceStateHandoff m_avboitDepthWarpInputStateHandoff;
    Core::CommandListResourceStateHandoff m_avboitDepthWarpStateHandoff;
    Core::CommandListResourceStateHandoff m_avboitExtinctionInputStateHandoff;
    Core::CommandListResourceStateHandoff m_avboitExtinctionStateHandoff;
    Core::CommandListResourceStateHandoff m_avboitIntegrationInputStateHandoff;
    Core::CommandListResourceStateHandoff m_avboitIntegrationStateHandoff;
    Core::CommandListResourceStateHandoff m_avboitAccumulationInputStateHandoff;
    Core::CommandListResourceStateHandoff m_avboitStateHandoff;
    Core::CommandListHandle m_meshViewSetupCommandList;
    Core::CommandListHandle m_sceneShadingSetupCommandList;
    Core::CommandListHandle m_deferredClearCommandList;
    Core::CommandListHandle m_gbufferCommandList;
    Core::CommandListHandle m_postGbufferNormalizeCommandList;
    // A small Graphics recovery packet retires an accepted frame timing scope when a later dependent packet is
    // rejected.  If it joins AsyncCompute, cross-lane resources remain concurrently shared.
    Core::CommandListHandle m_frameRecoveryCommandList;
    // Hardware dispatch-rays caustics retain this Graphics list until their later graph-migration step.
    Core::CommandListHandle m_hardwareCausticsCommandList;
    // The hybrid AVBOIT packet uses Graphics lists for raster phases and AsyncCompute lists for its two pure dispatches.
    Core::CommandListHandle m_avboitCommandList;
    Core::CommandListHandle m_asyncAvboitDepthWarpCommandList;
    Core::CommandListHandle m_avboitExtinctionCommandList;
    Core::CommandListHandle m_asyncAvboitIntegrationCommandList;
    Core::CommandListHandle m_avboitAccumulateCommandList;
    // Surface images are not required to support storage writes, so a short Graphics raster present pass follows the
    // composite result. The normal dedicated schedule obtains that result from Compute; the optional lagged schedule
    // keeps lighting/composite on Graphics.
    Core::CommandListHandle m_deferredPresentCommandList;
    Core::CommandListHandle m_shadowPrepareCommandList;
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

