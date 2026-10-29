// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/shared/renderer_frame_types.h>
#include <impl/ecs_render/raytrace/graph_snapshots.h>
#include <impl/ecs_render/refraction/statistics_readback.h>
#include <impl/ecs_render/raytrace/soft_shadow_wavelet.h>
#include <impl/ecs_render/caustic/settings.adhd.h>
#include <impl/ecs_render/raytrace/caustic_resolve_activity.h>
#include <impl/ecs_render/raytrace/scene_resources.h>
#include <impl/ecs_render/raytrace/optical_scene_resources.h>
#include <impl/ecs_render/raytrace/shadow_trace_geometry.h>
#include <impl/ecs_render/raytrace/prepared_builds.h>
#include <impl/ecs_render/raytrace/prepared_scene_bvh_upload.h>
#include <impl/ecs_render/raytrace/prepared_scene_tlas_build.h>
#include <impl/ecs_render/raytrace/prepared_shadow_material_context.h>
#include <impl/ecs_render/raytrace/software_scene_refit.h>
#include <impl/ecs_render/shadow/light_space_shadow.h>
#include <impl/ecs_render/shadow/quality_settings.adhd.h>

#include <core/alloc/scratch.h>
#include <core/graphics/gpu_timing.h>
#include <core/task/gpu/task_graph.h>

#include <global/simdmath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GraphicsRuntime;

namespace ECS{
    class World;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ComputeShader;
class ShadowSubsystem;
class ShadowMaterialSampledTextureCollector;
class RendererShaderSystem;
class RendererMeshSystem;
class RendererMaterialSystem;
class RendererRayTracingState;
class RendererOpticalVolumeSelection;
class CsgShapeRegistry;
struct MaterialSurfaceInfo;

namespace ECSRenderDetail{
    struct MeshRayTracingResourceSnapshot;
    struct MeshViewBufferSnapshot;
    struct SceneLightGpuData;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RayTracingSurfelGiTaskDetail{
    struct SurfelGiAgeFreeGraphTask;
    struct SurfelGiHashBuildGraphTask;
    struct SurfelGiSpawnGraphTask;
    struct SurfelGiTraceBuildArgsGraphTask;
    struct SurfelGiTraceGraphTask;
    struct SurfelGiResolveGraphTask;
    struct SurfelGiGraphTask;
};

namespace RayTracingShadowVisibilityTaskDetail{
    struct ShadowVisibilityOpaqueGraphTask;
    struct ShadowVisibilityOpaqueFirstWaveletGraphTask;
    struct ShadowVisibilityOpaqueResolveTailGraphTask;
    struct ShadowTransparentSoftTraceGraphTask;
    struct ShadowTransparentSoftTemporalMergeGraphTask;
    struct ShadowTransparentSoftFirstWaveletGraphTask;
    struct ShadowTransparentSoftFoldGraphTask;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererRayTracingSystem final : NoCopy{
    friend struct RayTracingShadowVisibilityTaskDetail::ShadowVisibilityOpaqueGraphTask;
    friend struct RayTracingShadowVisibilityTaskDetail::ShadowVisibilityOpaqueFirstWaveletGraphTask;
    friend struct RayTracingShadowVisibilityTaskDetail::ShadowVisibilityOpaqueResolveTailGraphTask;
    friend struct RayTracingShadowVisibilityTaskDetail::ShadowTransparentSoftTraceGraphTask;
    friend struct RayTracingShadowVisibilityTaskDetail::ShadowTransparentSoftTemporalMergeGraphTask;
    friend struct RayTracingShadowVisibilityTaskDetail::ShadowTransparentSoftFirstWaveletGraphTask;
    friend struct RayTracingShadowVisibilityTaskDetail::ShadowTransparentSoftFoldGraphTask;
    friend struct RayTracingSurfelGiTaskDetail::SurfelGiAgeFreeGraphTask;
    friend struct RayTracingSurfelGiTaskDetail::SurfelGiHashBuildGraphTask;
    friend struct RayTracingSurfelGiTaskDetail::SurfelGiSpawnGraphTask;
    friend struct RayTracingSurfelGiTaskDetail::SurfelGiTraceBuildArgsGraphTask;
    friend struct RayTracingSurfelGiTaskDetail::SurfelGiTraceGraphTask;
    friend struct RayTracingSurfelGiTaskDetail::SurfelGiResolveGraphTask;
    friend struct RayTracingSurfelGiTaskDetail::SurfelGiGraphTask;
    friend class SurfelGiTaskBuilder;


private:
    struct SurfelGiInitializationLifecycleGraphTask;


public:
    RendererRayTracingSystem(
        Core::Alloc::GlobalArena& arena,
        Core::ECS::World& world,
        Core::GraphicsRuntime& graphics,
        RendererShaderSystem& shaderSystem,
        RendererMeshSystem& meshSystem,
        RendererMaterialSystem& materialSystem,
        RendererRayTracingState& rayTracingState,
        const RendererOpticalVolumeSelection& opticalVolumes,
        const CsgShapeRegistry& csgShapeRegistry
    )noexcept;
    ~RendererRayTracingSystem()noexcept;


public:
    // Domain invalidation retires every descriptor generation before dropping its backing handles.
    void invalidateResources();
    void releaseSceneTlasHeapHandle();

    [[nodiscard]] RayTracingLightingClassificationInput snapshotLightingClassificationInput()const noexcept;
    void publishPreparedLightingClassification(const RayTracingLightingClassification& classification, Span<const ECSRenderDetail::SceneLightGpuData> lights);
    [[nodiscard]] RayTracingFrameCpuStateSnapshot captureFrameCpuState()const noexcept;
    void restoreShadowPacketCpuState(const RayTracingFrameCpuStateSnapshot& snapshot)noexcept;
    void restoreCausticPacketCpuState(const RayTracingFrameCpuStateSnapshot& snapshot)noexcept;
    void restoreSurfelGiPacketCpuState(const RayTracingFrameCpuStateSnapshot& snapshot)noexcept;
    void restorePreparedLightingCpuState(const RayTracingFrameCpuStateSnapshot& snapshot)noexcept;

    [[nodiscard]] RayTracingShadowPreparationResourceSnapshot snapshotShadowPreparationResources()const noexcept;
    [[nodiscard]] RayTracingDeferredGraphResourceSnapshot snapshotDeferredGraphResources()const noexcept;
    [[nodiscard]] b8 prepareSceneQueryResources();
    [[nodiscard]] RayTracingSceneGraphResources snapshotSceneGraphResources()const noexcept;
    [[nodiscard]] RayTracingSurfelPersistentResourceSnapshot snapshotSurfelPersistentResources()const noexcept;
    [[nodiscard]] RayTracingShadowVisibilityGraphPlanSnapshot snapshotShadowVisibilityGraphPlan(b8 hardwareShadowSupported)const noexcept;
    [[nodiscard]] b8 surfelCountReadbackSubmissionMatches(const Core::QueueSubmissionToken& submissionToken)const noexcept;
    void confirmSurfelCountReadbackSubmission(const Core::QueueSubmissionToken& submissionToken)noexcept;

    [[nodiscard]] b8 setCausticQualitySettings(const CausticQualitySettings& settings)noexcept;
    [[nodiscard]] b8 setSoftwareShadowSettings(const SoftwareShadowSettings& settings)noexcept;
    [[nodiscard]] b8 setShadowQualitySettings(const ShadowQualitySettings& settings)noexcept;
    [[nodiscard]] u32 requestedShadowReceiverFactor()const noexcept;
    [[nodiscard]] LightSpaceShadowSnapshot lightSpaceShadowSnapshot()const noexcept;
    void acceptLightSpaceShadowCapture(const LightSpaceCaptureTicket& ticket, b8 prepared);
    void invalidateLightSpaceShadowCapture()noexcept;

    void logCapabilityOnce();

    [[nodiscard]] b8 prepareRefractionResources();
    void setRefractionHardwareTracingEnabled(b8 enabled)noexcept;
    void setRefractionDiagnosticsEnabled(b8 enabled)noexcept;
    void pollRefractionStatistics();
    [[nodiscard]] Expected<RefractionStatistics> tryGetLatestRefractionStatistics()const noexcept;
    [[nodiscard]] RayTracingRefractionGraphResources snapshotRefractionGraphResources(u32 width, u32 height, u32 frameIndex)const noexcept;
    [[nodiscard]] b8 recordRefractionTileClassification(Core::CommandList& commandList, const DeferredFrameTargets& targets, const RayTracingRefractionGraphResources& resources)const;
    [[nodiscard]] b8 recordRefractionResolve(Core::CommandList& commandList, const DeferredFrameTargets& targets, const RayTracingRefractionGraphResources& resources)const;

    // Identity freezes in the shared graph. Select/grow trace resources before compile; the prep packet works only on that set.
    [[nodiscard]] b8 preflightShadowVisibilityResources(DeferredFrameTargets& targets, Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] Expected<b8> recordPreflightShadowVisibilityResources(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        b8 sceneTlasBuildGraphOwned,
        b8 meshBlasBuildsGraphOwned,
        b8 meshBlasGeometryBuildInputStatesGraphOwned,
        b8 meshSwBvhBuildsGraphOwned,
        b8 preparedMeshSwBvhBuildsRecordedByGraph
    );
    [[nodiscard]] b8 shadowVisibilityResourcesPreflighted()const noexcept{ return m_shadowVisibilityResourcesPreflighted; }

    [[nodiscard]] b8 shadowVisibilityHardwareSupported()const noexcept{ return m_shadowVisibilityHardwareSupported; }
    [[nodiscard]] b8 shadowVisibilitySoftwareResourcesPreflighted()const noexcept;
    void discardPreflightShadowVisibilityResources()noexcept;
    // Retained handles for the frozen plan. The graph imports each buffer once and shares IDs across staging packets.
    [[nodiscard]] b8 freezePreparedShadowTraceGeometryBuffers(Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] Span<const PreparedShadowTraceGeometryBuffer> preparedShadowTraceGeometryBuffers()const noexcept{ return m_preparedShadowTraceGeometryBuffers; }

    [[nodiscard]] Span<const Core::TextureHandle> preparedShadowTraceMaterialSampledTextures()const noexcept{ return m_preparedShadowTraceMaterialSampledTextures; }
    // Includes accepted-but-invisible streams; they keep their state source until removed or superseded.
    [[nodiscard]] Span<const Core::BufferHandle> acceptedShadowTraceGeometryBuffers()const noexcept{ return m_acceptedShadowTraceGeometryBuffers; }
    void confirmPreparedShadowTraceGeometryNormalization()noexcept;
    void invalidatePreparedShadowTraceGeometryBuffers()noexcept;

    void releaseCausticEmissionTargetHeapHandle();
    [[nodiscard]] b8 createShadowVisibilityTarget(DeferredFrameTargets& targets);
    [[nodiscard]] b8 createCausticTargets(DeferredFrameTargets& targets);
    [[nodiscard]] b8 createSurfelTargets(DeferredFrameTargets& targets);
    // Resolve the frozen shared material-context heap slots after preflight has settled all backing-buffer capacities.
    // The shared graph retains this POD as an immutable upload blob before recording begins.
    [[nodiscard]] Expected<RayTraceMaterialContextSlots> snapshotRayTraceMaterialContextSlots();
    [[nodiscard]] Expected<Optional<Core::GpuUploadBlobId>> retainPreparedCsgTraceContextUpload(Core::GpuTaskGraph& graph)const;
    [[nodiscard]] b8 prepareCausticOpticalReadTracking(const ECSRenderDetail::MeshViewBufferSnapshot& meshView);
    void confirmRayTraceContextReadSubmission(const Core::QueueSubmissionToken& token)noexcept;
    // Retain the preflight caustic AABB stream as a graph blob. Valid-empty means no refractive targets this frame.
    [[nodiscard]] Expected<Optional<Core::GpuUploadBlobId>> retainPreparedCausticEmissionTargetUpload(Core::GpuTaskGraph& graph)const;
    // Retain the exact per-frame surfel constant payload before graph recording. A valid empty result represents an inactive surfel frame.
    [[nodiscard]] Expected<Optional<Core::GpuUploadBlobId>> retainPreparedSurfelFrameConstantsUpload(Core::GpuTaskGraph& graph, const DeferredFrameTargets& targets)const;
    // The material table, instance stream, and typed bytes share indices and offsets. A fresh context retains one all-or-nothing upload batch.
    [[nodiscard]] Expected<PreparedShadowMaterialContextUploads> retainPreparedShadowMaterialContextUploads(Core::GpuTaskGraph& graph)const;
    void confirmPreparedShadowMaterialContextUploads()noexcept;
    // Scene hierarchy and leaf instances share indices; keep one preflight batch, publish both on Shadow Preparation submit.
    [[nodiscard]] Expected<PreparedSceneBvhUploads> retainPreparedSceneBvhUploads(Core::GpuTaskGraph& graph)const;
    void confirmPreparedSceneBvhUploads()noexcept;
    // Hardware TLAS work records from this frozen preflight plan in Shadow Preparation. Its static cache becomes valid only after that packet accepts.
    [[nodiscard]] b8 preparedSceneTlasBuildReady()const noexcept{ return m_preparedSceneTlas.ready(); }
    // Only newly allocated backing has a descriptor-native source; graph imports observe its current accepted generation.
    [[nodiscard]] Core::ResourceStates::Mask sceneTlasBackingInitialState()const noexcept;
    void confirmPreparedSceneTlasBuild()noexcept;
    // Publish acceleration-structure handoffs only after the Shadow Preparation packet accepts.
    // Hardware BLAS build/refit choices retain their selected handles through recording.
    // Only Shadow Preparation acceptance publishes a frozen plan's mesh-cache progress.
    [[nodiscard]] b8 preparedMeshBlasBuildsReady()const noexcept{ return m_preparedMeshBlasBuildsReady; }

    [[nodiscard]] Span<const PreparedMeshBlasBuild> preparedMeshBlasBuilds()const noexcept{ return m_preparedMeshBlasBuilds; }
    void confirmPreparedMeshBlasBuilds();
    // Software frames freeze build/refit work on the shared scratch generation; scene/material snapshots stay graph-owned.
    [[nodiscard]] const SoftwareSceneRefitHandle& preparedSceneSwBvhRefit()const noexcept{ return m_preparedSceneSwBvhRefit; }

    [[nodiscard]] b8 preparedMeshSwBvhBuildPlanFrozen()const noexcept{ return m_preparedMeshSwBvhBuildPlanFrozen; }

    [[nodiscard]] b8 preparedMeshSwBvhBuildsReady()const noexcept{ return m_preparedMeshSwBvhBuildsReady; }

    [[nodiscard]] Span<const PreparedMeshSwBvhBuild> preparedMeshSwBvhBuilds()const noexcept{ return m_preparedMeshSwBvhBuilds; }
    // Shadow Preparation records each frozen build after typed sentinel clears. Revalidate right before its compute sequence.
    [[nodiscard]] b8 recordPreparedMeshSwBvhBuildAfterGraphClears(Core::CommandList& commandList, const PreparedMeshSwBvhBuild& build);
    void confirmPreparedMeshSwBvhBuilds();
    void releaseRayTraceMaterialContextHeapHandles();
    void releaseSwBvhScratchHeapHandles();
    void releaseSurfelGiHeapHandles();
    // The deferred graph declares hardware trace entry resources before recording.
    [[nodiscard]] Expected<u32> renderShadowVisibility(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        const DeferredLightingGraphResources& deferredLightingResources,
        b8 splitSoftTransparentFold = false,
        b8 graphOwnsOpaqueTemporalMergeEntryStates = false,
        b8 splitOpaqueSoftResolve = false,
        const LightSpaceShadowSnapshot* lightSpace = nullptr
    );
    [[nodiscard]] Expected<Core::GpuTaskId> declareShadowVisibilityTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        const DeferredLightingGraphResources& deferredLightingResources,
        const b8& prepared,
        b8 hardwareShadowSupported,
        GraphOwnedAdaptiveShadowPlan graphOwnedAdaptivePlan,
        DeferredFrameTargets& targets,
        Core::GpuTimingSubmissionTicket& timingTicket
    );
    // A soft-transparent frame splits opaque visibility and transparent fold in one packet. The opaque task opens timing scopes.
    [[nodiscard]] Expected<Core::GpuTaskId> declareShadowVisibilityOpaqueTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        const DeferredLightingGraphResources& deferredLightingResources,
        const b8& prepared,
        b8 hardwareShadowSupported,
        b8 graphOwnsOpaqueTemporalMergeEntryStates,
        const LightSpaceShadowSnapshot* lightSpace,
        DeferredFrameTargets& targets,
        Core::GpuTimingSubmissionTicket& timingTicket,
        Optional<Core::GpuTimingMeasure>& asyncTiming,
        Optional<Core::GpuTimingMeasure>& shadowVisibilityTiming,
        b8& opaqueProduced,
        u32& opaqueFrameIndex
    );
    // The prepared opaque producer records the trace and geometry downsample first.
    // This adjacent callback owns the compiler-lowered trace/geometry sampled handoff before temporal merge and the first wavelet.
    [[nodiscard]] Expected<Core::GpuTaskId> declareShadowVisibilityOpaqueFirstWaveletTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        const DeferredLightingGraphResources& deferredLightingResources,
        const u32& opaqueFrameIndex,
        b8 hardwareShadowSupported,
        b8 graphOwnsOpaqueTemporalMergeEntryStates,
        b8 deferUpsample,
        b8 deferWavelet,
        DeferredFrameTargets& targets,
        Core::GpuTimingSubmissionTicket& timingTicket,
        Optional<Core::GpuTimingMeasure>& asyncTiming,
        Optional<Core::GpuTimingMeasure>& shadowVisibilityTiming,
        Optional<Core::GpuTimingMeasure>& opaqueResolveTiming,
        b8& opaqueProduced
    );
    [[nodiscard]] Expected<Core::GpuTaskId> declareShadowVisibilityOpaqueResolveTailTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        const DeferredLightingGraphResources& deferredLightingResources,
        const u32& opaqueFrameIndex,
        b8 hardwareShadowSupported,
        DeferredFrameTargets& targets,
        Core::GpuTimingSubmissionTicket& timingTicket,
        Optional<Core::GpuTimingMeasure>& asyncTiming,
        Optional<Core::GpuTimingMeasure>& shadowVisibilityTiming,
        Optional<Core::GpuTimingMeasure>& opaqueResolveTiming,
        b8& opaqueProduced
    );
    // The prepared transparent temporal merge receives frozen history/moment entry states and starts the resolve timing interval.
    // Its output reaches the first wavelet through the compiler-owned task handoff.
    [[nodiscard]] Expected<Core::GpuTaskId> declareShadowTransparentSoftTemporalMergeTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        const DeferredLightingGraphResources& deferredLightingResources,
        const b8& opaqueProduced,
        const u32& opaqueFrameIndex,
        b8 graphOwnsTransparentTemporalMergeEntryStates,
        b8 combinedTemporal,
        b8 hardwareShadowSupported,
        DeferredFrameTargets& targets,
        Core::GpuTimingSubmissionTicket& timingTicket,
        Optional<Core::GpuTimingMeasure>& transparentResolveTiming,
        b8& transparentTraceProduced
    );
    // The prepared transparent trace or temporal merge's first wavelet inherits its graph-declared input and output states.
    // The terminal fold remains responsible for the final upsample, submission acceptance, and history publication.
    [[nodiscard]] Expected<Core::GpuTaskId> declareShadowTransparentSoftFirstWaveletTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        const DeferredLightingGraphResources& deferredLightingResources,
        const b8& opaqueProduced,
        const u32& opaqueFrameIndex,
        b8 graphOwnsTransparentWaveletInputBoundary,
        b8 startsTransparentResolveTiming,
        b8 combinedWavelet,
        DeferredFrameTargets& targets,
        Core::GpuTimingSubmissionTicket& timingTicket,
        Optional<Core::GpuTimingMeasure>& transparentResolveTiming,
        b8& transparentTraceProduced
    );
    [[nodiscard]] Expected<Core::GpuTaskId> declareShadowTransparentSoftFoldTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        const DeferredLightingGraphResources& deferredLightingResources,
        const b8& opaqueProduced,
        const u32& opaqueFrameIndex,
        b8 combinedUpsample,
        DeferredFrameTargets& targets,
        Core::GpuTimingSubmissionTicket& timingTicket,
        Optional<Core::GpuTimingMeasure>& asyncTiming,
        Optional<Core::GpuTimingMeasure>& shadowVisibilityTiming,
        Optional<Core::GpuTimingMeasure>& transparentResolveTiming,
        b8& transparentTraceProduced
    );
    [[nodiscard]] Expected<Core::GpuTaskId> declareShadowTransparentSoftTraceTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        const DeferredLightingGraphResources& deferredLightingResources,
        const b8& opaqueProduced,
        const u32& opaqueFrameIndex,
        const LightSpaceShadowSnapshot* lightSpace,
        DeferredFrameTargets& targets,
        Core::GpuTimingSubmissionTicket& timingTicket,
        b8& transparentTraceProduced
    );
    void clearShadowVisibility(Core::CommandList& commandList, DeferredFrameTargets& targets);
    void confirmCausticAccumulatorNonTemporalClear()noexcept;
    // The temporal bootstrap clear is recorded by a graph task, but this mirror changes only when the containing caustic producer packet accepts.
    void confirmCausticAccumulatorBootstrapClear()noexcept;
    // A warm accumulator decays in its own task before the photon producer, sharing its packet; the compiler owns the UAV handoff.
    [[nodiscard]] Expected<Core::GpuTaskId> declareCausticAccumulatorDecayTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        const ECSRenderDetail::MeshViewBufferSnapshot& meshView,
        const b8& shadowVisibilityPrepared,
        f32 decayFactor,
        b8 hardwareCaustics,
        DeferredFrameTargets& targets,
        Core::GpuTimingSubmissionTicket& timingTicket,
        Optional<Core::GpuTimingMeasure>& causticPhotonTiming
    );
    // Record the decay dispatch itself. Graph callers leave entry state lowering and the following producer's UAV dependency to the compiler.
    [[nodiscard]] b8 dispatchCausticAccumulatorDecay(Core::CommandList& commandList, DeferredFrameTargets& targets, f32 decayFactor);
    // Software visibility consumes graph-declared traversal inputs for opaque shadows and transparent transmittance.
    [[nodiscard]] Expected<u32> renderGpuBvhShadowVisibility(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        const DeferredLightingGraphResources& deferredLightingResources,
        b8 splitSoftTransparentFold = false,
        b8 graphOwnsOpaqueTemporalMergeEntryStates = false,
        b8 splitOpaqueSoftResolve = false,
        const GraphOwnedAdaptiveShadowPlan* graphOwnedAdaptivePlan = nullptr,
        const LightSpaceShadowSnapshot* lightSpace = nullptr
    );
    [[nodiscard]] b8 prepareGpuBvhCausticResources(DeferredFrameTargets& targets);
    [[nodiscard]] Expected<Core::GpuTaskId> declareSoftwareCausticsTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        const DeferredLightingGraphResources& deferredLightingResources,
        const ECSRenderDetail::MeshViewBufferSnapshot& meshView,
        const b8& shadowVisibilityPrepared,
        b8 graphOwnsAccumulatorBootstrapClear,
        b8 graphOwnsNonTemporalAccumulatorClear,
        DeferredFrameTargets& targets,
        Core::GpuTimingSubmissionTicket& timingTicket,
        Optional<Core::GpuTimingMeasure>& causticPhotonTiming,
        b8& causticProducerDispatched
    );
    [[nodiscard]] b8 hasCausticWork(const ECSRenderDetail::MeshViewBufferSnapshot& meshView)const noexcept;
    [[nodiscard]] b8 renderGpuBvhCaustics(Core::CommandList& commandList, const ECSRenderDetail::MeshViewBufferSnapshot& meshView, DeferredFrameTargets& targets, Optional<Core::GpuTimingMeasure>* causticPhotonTiming = nullptr);
    [[nodiscard]] b8 prepareHwCausticResources(DeferredFrameTargets& targets);
    [[nodiscard]] Expected<Core::GpuTaskId> declareHardwareCausticsTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        const DeferredLightingGraphResources& deferredLightingResources,
        const ECSRenderDetail::MeshViewBufferSnapshot& meshView,
        const b8& shadowVisibilityPrepared,
        b8 graphOwnsAccumulatorBootstrapClear,
        b8 graphOwnsNonTemporalAccumulatorClear,
        DeferredFrameTargets& targets,
        Core::GpuTimingSubmissionTicket& timingTicket,
        Optional<Core::GpuTimingMeasure>& causticPhotonTiming,
        b8& causticProducerDispatched
    );
    [[nodiscard]] b8 renderHwCaustics(Core::CommandList& commandList, const ECSRenderDetail::MeshViewBufferSnapshot& meshView, DeferredFrameTargets& targets, Optional<Core::GpuTimingMeasure>* causticPhotonTiming = nullptr);
    // Records downsample, resolve prepare, wavelets, upsample after the photon producer. Uses own the UAV-to-SRV handoffs.
    [[nodiscard]] Expected<Core::GpuTaskId> declareCausticGeometryDownsampleTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        const b8& causticProducerDispatched,
        DeferredFrameTargets& targets,
        Core::GpuTimingSubmissionTicket& timingTicket,
        Optional<Core::GpuTimingMeasure>& causticResolveTiming
    );
    [[nodiscard]] CausticResolveActivitySnapshot causticResolveActivitySnapshot(const DeferredFrameTargets& targets)const noexcept;
    [[nodiscard]] Expected<Core::GpuTaskId> declareCausticResolveTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        const b8& causticProducerDispatched,
        Core::GpuTimingSubmissionTicket& timingTicket,
        Optional<Core::GpuTimingMeasure>& causticResolveTiming
    );
    [[nodiscard]] Expected<Core::GpuTaskId> declareCausticResolvePrepareTask(Core::GpuTaskGraph& graph, const Core::GpuTaskDesc& desc, const b8& causticProducerDispatched, DeferredFrameTargets& targets);
    [[nodiscard]] Expected<Core::GpuTaskId> declareCausticResolveWaveletTask(Core::GpuTaskGraph& graph, const Core::GpuTaskDesc& desc, const b8& causticProducerDispatched, DeferredFrameTargets& targets);
    [[nodiscard]] Expected<Core::GpuTaskId> declareCausticResolveSecondWaveletTask(Core::GpuTaskGraph& graph, const Core::GpuTaskDesc& desc, const b8& causticProducerDispatched, DeferredFrameTargets& targets);
    [[nodiscard]] Expected<Core::GpuTaskId> declareCausticResolveThirdWaveletTask(Core::GpuTaskGraph& graph, const Core::GpuTaskDesc& desc, const b8& causticProducerDispatched, DeferredFrameTargets& targets);
    [[nodiscard]] Expected<Core::GpuTaskId> declareCausticResolveFourthWaveletTask(Core::GpuTaskGraph& graph, const Core::GpuTaskDesc& desc, const b8& causticProducerDispatched, DeferredFrameTargets& targets);
    [[nodiscard]] Expected<Core::GpuTaskId> declareCausticResolveFifthWaveletTask(Core::GpuTaskGraph& graph, const Core::GpuTaskDesc& desc, const b8& causticProducerDispatched, DeferredFrameTargets& targets);
    [[nodiscard]] Expected<Core::GpuTaskId> declareCausticResolveUpsampleTask(Core::GpuTaskGraph& graph, const Core::GpuTaskDesc& desc, const b8& causticProducerDispatched, DeferredFrameTargets& targets);
    // Typed stages own immutable inputs and each ping-pong handoff.
    void dispatchCausticGeometryDownsample(Core::CommandList& commandList, DeferredFrameTargets& targets);
    void dispatchCausticResolvePrepare(Core::CommandList& commandList, DeferredFrameTargets& targets);
    void dispatchCausticResolveWaveletPass(Core::CommandList& commandList, DeferredFrameTargets& targets, u32 passIndex);
    void dispatchCausticResolveUpsample(Core::CommandList& commandList, DeferredFrameTargets& targets);
    [[nodiscard]] b8 hasHwCausticWork()const noexcept;
    [[nodiscard]] b8 hasHwCausticWork(const ECSRenderDetail::MeshViewBufferSnapshot& meshView)const noexcept;
    [[nodiscard]] b8 hasSurfelWork()const noexcept;
    [[nodiscard]] b8 needsSurfelResourceInitialization()const noexcept;
    // Typed clear primitives own the buffer writes. This task only records and publishes CPU lifecycle after the producer packet accepts.
    [[nodiscard]] Expected<Core::GpuTaskId> declareSurfelResourceInitializationLifecycleTask(Core::GpuTaskGraph& graph, const Core::GpuTaskDesc& desc);
    [[nodiscard]] b8 recordSurfelResourceInitializationLifecycle()noexcept;
    // The initialization mirror commits only after the typed-clear producer packet accepts.
    void finalizeSurfelResourceInitialization()noexcept;
    void discardSurfelResourceInitialization()noexcept;
    // Persistent surfel storage survives resize.
    [[nodiscard]] b8 ensureSurfelResources();
    [[nodiscard]] b8 ensureSurfelSpawnPipeline();
    [[nodiscard]] b8 ensureSurfelAgeFreePipeline();
    [[nodiscard]] b8 ensureSurfelHashBuildPipeline();
    [[nodiscard]] b8 ensureSurfelTracePipeline();
    // Hardware surfel trace uses inline RayQuery over the TLAS.
    [[nodiscard]] b8 ensureSurfelTraceHwPipeline();
    // Resolve keeps the writable surfel pool off deferred lighting.
    [[nodiscard]] b8 ensureSurfelResolvePipeline();
    [[nodiscard]] b8 ensureSurfelUpsamplePipeline();
    [[nodiscard]] b8 ensureSurfelTraceBuildArgsPipeline();
    // The graph schedules the small diagnostic readback as a late copy task and publishes its token.
    [[nodiscard]] b8 shouldCaptureSurfelCountReadback()const noexcept;
    void markSurfelCountReadbackScheduled()noexcept;
    [[nodiscard]] b8 softTransparentShadowReady()const noexcept;
    // Commit soft-shadow history only after ordered submission accepts.
    void finalizeSoftShadowTemporalHistory(DeferredFrameTargets& targets)noexcept;
    void discardSoftShadowTemporalHistory()noexcept;


private:
    [[nodiscard]] b8 preparePendingMeshBlasResources(Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] b8 prepareSceneTlasResources(Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] b8 prepareSceneSwBvhResources(Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] b8 prepareCausticEmissionTargetResources(Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] b8 capturePreparedShadowMaterialContext(
        PreparedShadowMaterialContextRoute::Enum route,
        b8 staticScene,
        u64 hash,
        Span<const NwbRtInstanceMaterialGpu> instanceMaterials,
        Span<const InstanceGpuData> instances,
        Span<const u8> materialTypedBytes
    );
    [[nodiscard]] b8 preparedTraversalStorageMatchesCurrent()const noexcept;
    [[nodiscard]] b8 capturePreparedShadowMaterialContextCacheReuse(u64 hash, usize instanceMaterialCount, usize instanceCount, usize materialTypedByteCount)noexcept;
    void clearPreparedShadowMaterialContext()noexcept;
    [[nodiscard]] b8 appendPreparedShadowTraceMaterialSampledTextures(const MaterialSurfaceInfo& materialInfo, ShadowMaterialSampledTextureCollector& collector);
    void clearPreparedShadowTraceMaterialSampledTextures()noexcept;
    [[nodiscard]] b8 capturePreparedSceneBvh(b8 staticScene, u64 staticSceneHash, Span<const NwbBvhNodeGpu> nodes, Span<const SceneSwBvhInstanceGpu> instances);
    [[nodiscard]] b8 capturePreparedSceneBvhCacheReuse(u64 staticSceneHash, u32 instanceCount)noexcept;
    void clearPreparedSceneBvh()noexcept;
    [[nodiscard]] b8 capturePreparedSceneSwBvhTraversal(Span<const PreparedSceneSwBvhMesh> meshes, u32 instanceCount);
    // Preflight leaves descriptor tables populated. Frozen traversal checks tables and retained identities without republishing state.
    [[nodiscard]] b8 recordPreparedSceneSwBvhTraversal();
    void clearPreparedSceneSwBvhTraversal()noexcept;
    [[nodiscard]] b8 capturePreparedMeshBlasBuilds(Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] b8 recordPreparedMeshBlasBuilds(Core::CommandList& commandList, b8 meshBlasGeometryBuildInputStatesGraphOwned);
    void clearPreparedMeshBlasBuilds()noexcept;
    [[nodiscard]] b8 capturePreparedMeshSwBvhBuilds(Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] b8 preparedMeshSwBvhBuildMatchesCurrent(const PreparedMeshSwBvhBuild& build)noexcept;
    [[nodiscard]] b8 preparedMeshSwBvhBuildProducesTopology(const ECSRenderDetail::MeshRayTracingResourceSnapshot& mesh)const noexcept;
    void clearPreparedMeshSwBvhBuilds()noexcept;
    [[nodiscard]] b8 prepareSurfelResources(DeferredFrameTargets& targets);
    [[nodiscard]] b8 renderSurfelGiAgeFree(Core::CommandList& commandList, DeferredFrameTargets& targets, const DeferredLightingGraphResources& deferredLightingResources);
    [[nodiscard]] b8 renderSurfelGiUpsample(Core::CommandList& commandList, DeferredFrameTargets& targets, const DeferredLightingGraphResources& deferredLightingResources);
    [[nodiscard]] b8 renderSurfelGiHashBuild(Core::CommandList& commandList, DeferredFrameTargets& targets, const DeferredLightingGraphResources& deferredLightingResources);
    [[nodiscard]] b8 renderSurfelGiSpawn(Core::CommandList& commandList, DeferredFrameTargets& targets, const DeferredLightingGraphResources& deferredLightingResources);
    [[nodiscard]] b8 renderSurfelGiTraceBuildArgs(Core::CommandList& commandList, DeferredFrameTargets& targets, const DeferredLightingGraphResources& deferredLightingResources);
    [[nodiscard]] b8 renderSurfelGiTrace(Core::CommandList& commandList, DeferredFrameTargets& targets, const DeferredLightingGraphResources& deferredLightingResources);
    [[nodiscard]] b8 renderSurfelGiResolve(Core::CommandList& commandList, DeferredFrameTargets& targets, const DeferredLightingGraphResources& deferredLightingResources);
    [[nodiscard]] b8 renderSurfelGiPhases(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        const DeferredLightingGraphResources& deferredLightingResources,
        b8 dispatchAgeFree,
        b8 dispatchHashBuild,
        b8 dispatchSpawn,
        b8 dispatchTraceBuildArgs,
        b8 dispatchTrace,
        b8 dispatchResolve,
        b8 dispatchRemaining
    );
    [[nodiscard]] b8 prepareMeshBlasResources(ECSRenderDetail::MeshRayTracingResourceSnapshot& meshResources);
    // Runtime meshes prepare every frame; static meshes remain dirty until first build.
    [[nodiscard]] b8 preparePendingMeshSwBvhResources(Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] b8 ensureShadowPipeline();
    // Hardware soft trace feeds the shared software denoise chain.
    [[nodiscard]] b8 ensureShadowSoftPipeline();
    // Hardware shadow layouts are push-only; resources come from heap sets.
    void appendShadowTraceBindingLayout(Core::BindingLayoutDesc& layoutDesc)const;
    [[nodiscard]] b8 ensureSwShadowPipeline();
    [[nodiscard]] b8 ensureSwShadowPassPipeline(Core::ShaderHandle& shader, Core::ComputePipelineHandle& pipeline, const Core::Assets::AssetRef<ComputeShader> ShadowSubsystem::* shaderMember, const NameText<char> debugLabel);
    [[nodiscard]] b8 ensureSoftwareTransparentSamplingPipeline();
    [[nodiscard]] b8 ensureSoftShadowResolvePipeline();
    [[nodiscard]] b8 ensureShadowGeometryDownsamplePipeline();
    [[nodiscard]] b8 ensureSoftTransparentResolvePipeline();
    void prepareSoftCombinedResolvePipelines();
    [[nodiscard]] b8 ensureSoftCombinedUpsamplePipeline();
    [[nodiscard]] b8 ensureSoftCombinedWaveletPipeline();
    [[nodiscard]] b8 ensureSoftCombinedTemporalPipeline();
    [[nodiscard]] b8 renderSoftShadowCombinedTemporalMerge(Core::CommandList& commandList, DeferredFrameTargets& targets);
    [[nodiscard]] b8 renderSoftShadowCombinedWavelet(Core::CommandList& commandList, DeferredFrameTargets& targets);
    [[nodiscard]] b8 renderSoftShadowTerminalUpsample(Core::CommandList& commandList, DeferredFrameTargets& targets, const DeferredLightingGraphResources& deferredLightingResources, b8 transparentReady);

    [[nodiscard]] b8 prepareHardwareTransparentShadowResources(DeferredFrameTargets& targets);
    void releaseHardwareTransparentShadowResources();
    [[nodiscard]] b8 hardwareTransparentShadowReady()const noexcept;
    void dispatchHardwareTransparentShadow(Core::CommandList& commandList, DeferredFrameTargets& targets, u32 frameIndex);

    // Resolve a contiguous shadow-slot range in one heap-selected dispatch.
    void dispatchSoftShadowResolve(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        const DeferredLightingGraphResources& deferredLightingResources,
        u32 slotStart,
        u32 slotCount,
        const SoftShadowResolveDispatch& dispatch,
        b8 dispatchFirstWavelet = true,
        b8 dispatchTail = true
    );
    void enableSoftShadowResolveBarriers(Core::CommandList& commandList, DeferredFrameTargets& targets);
    void dispatchPrimarySoftShadowResolve(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        const DeferredLightingGraphResources& deferredLightingResources,
        u32 frameIndex,
        u32 softGroupsX,
        u32 softGroupsY,
        b8 splitOpaqueSoftResolve,
        b8 splitSoftTransparentFold,
        b8 graphOwnsOpaqueTemporalMergeEntryStates
    );
    // Denoise either backend's soft trace and optionally run the transparent trace and resolve phases.
    // The shared graph gives first downsample states and key handoffs; later transitions stay local.
    void dispatchSoftShadowDenoiseAndTransparentFold(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        const DeferredLightingGraphResources& deferredLightingResources,
        u32 frameIndex,
        u32 softGroupsX,
        u32 softGroupsY,
        b8 dispatchOpaqueGeometry = true,
        b8 dispatchOpaqueResolve = true,
        b8 dispatchTransparentTrace = true,
        b8 dispatchTransparentResolve = true,
        b8 graphOwnsOpaqueGeometryToResolveBoundary = false,
        b8 graphOwnsOpaqueToTransparentBoundary = false,
        b8 graphOwnsTransparentTraceToResolveBoundary = false,
        b8 graphOwnsOpaqueTemporalMergeEntryStates = false,
        b8 graphOwnsTransparentTemporalMergeEntryStates = false,
        b8 dispatchOpaqueResolveTail = true,
        b8 graphOwnsOpaqueTraceToFirstWaveletBoundary = false,
        b8 dispatchTransparentResolveTail = false,
        b8 splitTransparentResolve = false,
        b8 dispatchTransparentTemporalMerge = false,
        SoftShadowOpaqueResolvePhase::Enum opaquePhase = SoftShadowOpaqueResolvePhase::TemporalAndWavelet
    );
    // Graph-only phase helpers preserve the complete direct route above while exposing both in-packet handoffs to the shared deferred graph.
    [[nodiscard]] Expected<u32> renderShadowVisibilityOpaque(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        const DeferredLightingGraphResources& deferredLightingResources,
        b8 graphOwnsOpaqueTemporalMergeEntryStates,
        const LightSpaceShadowSnapshot* lightSpace
    );
    [[nodiscard]] b8 renderSoftOpaqueShadowResolvePhase(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        const DeferredLightingGraphResources& deferredLightingResources,
        u32 frameIndex,
        b8 hardwareShadowSupported,
        b8 graphOwnsOpaqueTemporalMergeEntryStates,
        SoftShadowOpaqueResolvePhase::Enum phase
    );
    [[nodiscard]] b8 renderSoftOpaqueShadowResolveTail(Core::CommandList& commandList, DeferredFrameTargets& targets, const DeferredLightingGraphResources& deferredLightingResources, u32 frameIndex, b8 hardwareShadowSupported);
    [[nodiscard]] Expected<u32> renderGpuBvhShadowVisibilityOpaque(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        const DeferredLightingGraphResources& deferredLightingResources,
        b8 graphOwnsOpaqueTemporalMergeEntryStates,
        const LightSpaceShadowSnapshot* lightSpace = nullptr
    );
    [[nodiscard]] b8 renderSoftTransparentShadowTrace(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        const DeferredLightingGraphResources& deferredLightingResources,
        u32 frameIndex,
        b8 graphOwnsOpaqueToTransparentBoundary,
        const LightSpaceShadowSnapshot* lightSpace = nullptr
    );
    void reportSoftwareShadowTraversal(const DeferredFrameTargets& targets);
    [[nodiscard]] b8 renderSoftTransparentShadowTemporalMerge(Core::CommandList& commandList, DeferredFrameTargets& targets, const DeferredLightingGraphResources& deferredLightingResources, u32 frameIndex, b8 graphOwnsTransparentTemporalMergeEntryStates);
    [[nodiscard]] b8 renderSoftTransparentShadowFirstWavelet(Core::CommandList& commandList, DeferredFrameTargets& targets, const DeferredLightingGraphResources& deferredLightingResources, u32 frameIndex, b8 graphOwnsTransparentWaveletInputBoundary);
    [[nodiscard]] b8 renderSoftTransparentShadowFold(Core::CommandList& commandList, DeferredFrameTargets& targets, const DeferredLightingGraphResources& deferredLightingResources, u32 frameIndex);
    // Temporal merge precedes soft resolve and swaps history at frame end.
    [[nodiscard]] b8 ensureShadowReprojectMergePipeline();
    [[nodiscard]] b8 softShadowTemporalHistoryUsable()const noexcept;
    [[nodiscard]] u32 transparentShadowSampleCount()const noexcept;
    void reportTransparentShadowSampling(u32 sampleCount);
    void swapSoftShadowTemporalHistory(DeferredFrameTargets& targets)noexcept;
    [[nodiscard]] b8 prepareCausticResources(const ECSRenderDetail::MeshViewBufferSnapshot& meshView, DeferredFrameTargets& targets, b8 hardware);
    [[nodiscard]] b8 ensureSwCausticPipeline();
    [[nodiscard]] b8 ensureCausticResolvePipeline();
    [[nodiscard]] b8 prepareCausticResolveActivity(u32 halfWidth, u32 halfHeight);
    void releaseCausticResolveActivity();
    [[nodiscard]] b8 ensureCausticGeometryDownsamplePipeline();
    [[nodiscard]] b8 causticResolveResourcesReady(const DeferredFrameTargets& targets, f32 temporalDecay)const noexcept;
    [[nodiscard]] b8 hasCommonCausticWork(const ECSRenderDetail::MeshViewBufferSnapshot& meshView)const noexcept;
    [[nodiscard]] b8 ensureCausticAccumulatorDecayPipeline();
    [[nodiscard]] b8 ensureCausticRtPipeline();
    [[nodiscard]] b8 ensureBvhSortPipeline();
    [[nodiscard]] b8 ensureBvhSortBuffers(usize paddedCount);
    [[nodiscard]] b8 bvhBitonicSort(Core::CommandList& commandList, u32 elementCount, u32 paddedCount);
    [[nodiscard]] b8 ensureBvhBuildPipeline();
    [[nodiscard]] b8 ensureBvhVisitCounterBuffer(usize primitiveCount);
    [[nodiscard]] b8 createMeshBvhStorage(usize primitiveCount, Core::BufferHandle& nodeBuffer, Core::BufferHandle& parentBuffer, Core::GpuDescriptorHandle& nodeHeapHandle, Core::GpuDescriptorHandle& parentHeapHandle);
    [[nodiscard]] b8 ensureMeshSwBvhResources(u32 primitiveCount, Core::BufferHandle& nodeBuffer, Core::BufferHandle& parentBuffer, Core::GpuDescriptorHandle& nodeHeapHandle, Core::GpuDescriptorHandle& parentHeapHandle);
    [[nodiscard]] b8 meshSwBvhResourcesReady(const Core::BufferHandle& nodeBuffer, const Core::BufferHandle& parentBuffer, Core::GpuDescriptorHandle nodeHeapHandle, Core::GpuDescriptorHandle parentHeapHandle)noexcept;
    [[nodiscard]] b8 buildMeshSwBvhPrepared(
        Core::CommandList& commandList, u32 positionHeapSlot, u32 triangleIndexHeapSlot, u32 primitiveCount, const Float3Int& aabbMin, const Float3Int& aabbMax,
        Core::GpuDescriptorHandle nodeHeapHandle, Core::GpuDescriptorHandle parentHeapHandle, Core::BufferHandle& nodeBuffer, Core::BufferHandle& parentBuffer
    );
    [[nodiscard]] b8 refitMeshSwBvhPrepared(
        Core::CommandList& commandList, u32 positionHeapSlot, u32 triangleIndexHeapSlot, u32 primitiveCount, Core::GpuDescriptorHandle nodeHeapHandle, Core::GpuDescriptorHandle parentHeapHandle, Core::BufferHandle& nodeBuffer, Core::BufferHandle& parentBuffer
    );
    void preflightLightSpaceShadowResources();
    [[nodiscard]] Expected<LightSpacePlan> buildLightSpaceShadowPlan(Span<const ECSRenderDetail::SceneLightGpuData> lights)const noexcept;
    void prepareLightSpaceShadows(Span<const ECSRenderDetail::SceneLightGpuData> lights);
    [[nodiscard]] b8 ensureLightSpaceShadowPipelines();
    [[nodiscard]] b8 ensureLightSpaceShadowStorage(const LightSpacePlan& plan);
    void releaseLightSpaceShadowResources();
    [[nodiscard]] b8 ensureSceneBvhBuffers(u32 instanceCount);
    [[nodiscard]] b8 ensureRayTraceMaterialContextSlotsBuffer();
    [[nodiscard]] b8 ensureRayTraceContextReadTracking(b8 opticalCausticReader);
    [[nodiscard]] b8 ensureCsgTraceContextResources();
    [[nodiscard]] b8 ensureRayTraceMaterialContextSlotsHeapHandle();
    [[nodiscard]] b8 ensureRayTraceMaterialContextHeapHandle(Core::Buffer& buffer, Core::GpuDescriptorHandle& handle);
    [[nodiscard]] b8 replaceRayTraceMaterialContextHeapHandle(Core::Buffer& buffer, Core::GpuDescriptorHandle& handle);
    [[nodiscard]] b8 ensureCausticEmissionTargetBuffer(usize targetCount);
    [[nodiscard]] b8 ensureShadowInstanceMaterialBuffer(usize instanceCount);
    [[nodiscard]] b8 ensureShadowInstanceContextBuffer(usize instanceCount);
    [[nodiscard]] b8 ensureShadowMaterialTypedBuffer(usize byteCount);


private:
    Core::Alloc::GlobalArena& m_arena;
    Core::ECS::World& m_world;
    Core::GraphicsRuntime& m_graphics;
    RendererShaderSystem& m_shaderSystem;
    RendererMeshSystem& m_meshSystem;
    RendererMaterialSystem& m_materialSystem;
    RendererRayTracingState& m_rayTracingState;
    const RendererOpticalVolumeSelection& m_opticalVolumes;
    const CsgShapeRegistry& m_csgShapeRegistry;
    RayTracingOpticalSceneResources m_hardwareOpticalScene;
    RayTracingOpticalSceneResources m_softwareOpticalScene;
    SoftwareSceneRefitResources m_sceneSwBvhRefit;
    LightSpaceShadowState m_lightSpaceShadow;
    RefractionStatisticsReadback m_refractionStatistics;
    b8 m_refractionDiagnosticsEnabled = false;
    SoftwareSceneRefitHandle m_preparedSceneSwBvhRefit;
    PreparedShadowTraceGeometryBufferVector m_preparedShadowTraceGeometryBuffers;
    Vector<Core::BufferHandle, Core::Alloc::GlobalArena> m_acceptedShadowTraceGeometryBuffers;
    PreparedShadowTraceMaterialSampledTextureVector m_preparedShadowTraceMaterialSampledTextures;
    // Retain packed NwbCausticEmissionTargetGpu bytes across declaration and recording without regathering state.
    Vector<u8, Core::Alloc::GlobalArena> m_preparedCausticEmissionTargetBytes;
    // Transient HW fallback keeps material context only (Shadow Prep already froze the TLAS/BLAS plan).
    PreparedShadowMaterialContext m_preparedShadowMaterialContext;
    PreparedSceneBvhUpload m_preparedSceneBvh;
    PreparedSceneTlasBuild m_preparedSceneTlas;
    // Fresh uploads and static-cache reuse both freeze the traversal table. Recording never rebuilds scene data; ECS versions reject stale plans.
    PreparedSceneSwBvhMeshVector m_preparedSceneSwBvhMeshes;
    u64 m_preparedSceneSwBvhRendererMutationVersion = 0u;
    u64 m_preparedSceneSwBvhTransformMutationVersion = 0u;
    u64 m_preparedSceneSwBvhMaterialMutationVersion = 0u;
    PreparedMeshBlasBuildVector m_preparedMeshBlasBuilds;
    // Per-frame BLAS actions keyed by dirty reason.
    u64 m_blasLedgerStaticSkipped = 0u;
    u64 m_blasLedgerFirstBuilds = 0u;
    u64 m_blasLedgerRefits = 0u;
    u64 m_blasLedgerRebuilds = 0u;
    u64 m_blasLedgerUploadedBytes = 0u;
    PreparedMeshSwBvhBuildVector m_preparedMeshSwBvhBuilds;
    DeferredFrameTargets* m_shadowVisibilityPreparedTargets = nullptr;
    RayTracingSceneContentStamp m_preparedSceneContentStamp;
    CausticQualitySettings m_causticQualitySettings;
    u32 m_preparedSceneSwBvhInstanceCount = 0u;
    ShadowQualitySettings m_shadowQualitySettings;
    b8 m_temporalOneShadowSamplingLogged = false;
    b8 m_preparedSceneSwBvhReady = false;
    b8 m_preparedMeshBlasBuildsReady = false;
    b8 m_preparedMeshSwBvhBuildsReady = false;
    b8 m_preparedMeshSwBvhBuildPlanFrozen = false;
    b8 m_shadowVisibilityResourcesPreflighted = false;
    b8 m_shadowVisibilityHardwareSupported = false;
    b8 m_opticalCausticReaderAdmitted = false;
    // Dedupe for the refractive-volume optical-boundary diagnostic: count, hash, first entity, reporting latch.
    u32 m_unsupportedCausticVolumeCount = 0u;
    u64 m_unsupportedCausticVolumeHash = 0u;
    u64 m_reportedUnsupportedCausticVolumeHash = 0u;
    u32 m_firstUnsupportedCausticVolumeEntity = 0u;
    b8 m_unsupportedCausticVolumeReported = false;
    // Recording may only touch allocations selected before the shared graph is compiled.
    // These flags distinguish a usable frozen trace plan from a non-fatal preflight fallback that leaves the effect black for this frame.
    b8 m_shadowVisibilityTraceResourcesPreflighted = false;
    b8 m_shadowVisibilityBackendPipelinePreflighted = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

