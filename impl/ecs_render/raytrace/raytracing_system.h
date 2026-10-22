// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/shared/renderer_frame_types.h>
#include <impl/ecs_render/raytrace/graph_snapshots.h>
#include <impl/ecs_render/raytrace/soft_shadow_wavelet.h>
#include <impl/ecs_render/caustic/settings.h>
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
#include <impl/ecs_render/shadow/quality_settings.h>

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
    void publishPreparedLightingClassification(const RayTracingLightingClassification& classification, const ECSRenderDetail::SceneLightGpuData* lights, u32 lightCount);
    [[nodiscard]] RayTracingFrameCpuStateSnapshot captureFrameCpuState()const noexcept;
    void restoreShadowPacketCpuState(const RayTracingFrameCpuStateSnapshot& snapshot)noexcept;
    void restoreCausticPacketCpuState(const RayTracingFrameCpuStateSnapshot& snapshot)noexcept;
    void restoreSurfelGiPacketCpuState(const RayTracingFrameCpuStateSnapshot& snapshot)noexcept;
    void restorePreparedLightingCpuState(const RayTracingFrameCpuStateSnapshot& snapshot)noexcept;

    [[nodiscard]] RayTracingShadowPreparationResourceSnapshot snapshotShadowPreparationResources()const noexcept;
    [[nodiscard]] RayTracingDeferredGraphResourceSnapshot snapshotDeferredGraphResources()const noexcept;
    [[nodiscard]] bool prepareSceneQueryResources();
    [[nodiscard]] RayTracingSceneGraphResources snapshotSceneGraphResources()const noexcept;
    [[nodiscard]] RayTracingSurfelPersistentResourceSnapshot snapshotSurfelPersistentResources()const noexcept;
    [[nodiscard]] RayTracingShadowVisibilityGraphPlanSnapshot snapshotShadowVisibilityGraphPlan(bool hardwareShadowSupported)const noexcept;
    [[nodiscard]] bool surfelCountReadbackSubmissionMatches(const Core::QueueSubmissionToken& submissionToken)const noexcept;
    void confirmSurfelCountReadbackSubmission(const Core::QueueSubmissionToken& submissionToken)noexcept;

    [[nodiscard]] bool setCausticQualitySettings(const CausticQualitySettings& settings)noexcept;
    [[nodiscard]] bool setSoftwareShadowSettings(const SoftwareShadowSettings& settings)noexcept;
    [[nodiscard]] bool setShadowQualitySettings(const ShadowQualitySettings& settings)noexcept;
    [[nodiscard]] u32 requestedShadowReceiverFactor()const noexcept;
    [[nodiscard]] LightSpaceShadowSnapshot lightSpaceShadowSnapshot()const noexcept;
    void acceptLightSpaceShadowCapture(const LightSpaceCaptureTicket& ticket, bool prepared);
    void invalidateLightSpaceShadowCapture()noexcept;

    void logCapabilityOnce();

    [[nodiscard]] bool prepareRefractionResources();
    void setRefractionHardwareTracingEnabled(bool enabled)noexcept;
    [[nodiscard]] RayTracingRefractionGraphResources snapshotRefractionGraphResources()const noexcept;
    [[nodiscard]] bool recordRefractionResolve(Core::CommandList& commandList, const DeferredFrameTargets& targets, const RayTracingRefractionGraphResources& resources)const;

    // Resource identity is frozen by the shared deferred graph.  Select/grow every trace resource before compilation, then let the preparation packet issue only work against that frozen set.
    [[nodiscard]] bool preflightShadowVisibilityResources(DeferredFrameTargets& targets, Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] Expected<bool> recordPreflightShadowVisibilityResources(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        bool sceneTlasBuildGraphOwned,
        bool meshBlasBuildsGraphOwned,
        bool meshBlasGeometryBuildInputStatesGraphOwned,
        bool meshSwBvhBuildsGraphOwned,
        bool preparedMeshSwBvhBuildsRecordedByGraph
    );
    [[nodiscard]] bool shadowVisibilityResourcesPreflighted()const noexcept{ return m_shadowVisibilityResourcesPreflighted; }

    [[nodiscard]] bool shadowVisibilityHardwareSupported()const noexcept{ return m_shadowVisibilityHardwareSupported; }
    [[nodiscard]] bool shadowVisibilitySoftwareResourcesPreflighted()const noexcept;
    void discardPreflightShadowVisibilityResources()noexcept;
    // These are retained handles for the current frozen trace plan. The graph imports each physical buffer once and uses the shared IDs for every packet that manually stages it.
    [[nodiscard]] bool freezePreparedShadowTraceGeometryBuffers(Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] const PreparedShadowTraceGeometryBufferVector& preparedShadowTraceGeometryBuffers()const noexcept{ return m_preparedShadowTraceGeometryBuffers; }

    [[nodiscard]] const PreparedShadowTraceMaterialSampledTextureVector& preparedShadowTraceMaterialSampledTextures()const noexcept{ return m_preparedShadowTraceMaterialSampledTextures; }
    // Includes accepted but currently invisible mesh streams, which must retain their state source until their owning mesh is removed or a later preparation packet supersedes it.
    [[nodiscard]] const Vector<Core::BufferHandle, Core::Alloc::GlobalArena>& acceptedShadowTraceGeometryBuffers()const noexcept{ return m_acceptedShadowTraceGeometryBuffers; }
    void confirmPreparedShadowTraceGeometryNormalization()noexcept;
    void invalidatePreparedShadowTraceGeometryBuffers()noexcept;

    void releaseCausticEmissionTargetHeapHandle();
    [[nodiscard]] bool createShadowVisibilityTarget(DeferredFrameTargets& targets);
    [[nodiscard]] bool createCausticTargets(DeferredFrameTargets& targets);
    [[nodiscard]] bool createSurfelTargets(DeferredFrameTargets& targets);
    // Resolve the frozen shared material-context heap slots after preflight has settled all backing-buffer capacities.
    // The shared graph retains this POD as an immutable upload blob before recording begins.
    [[nodiscard]] Expected<RayTraceMaterialContextSlots> snapshotRayTraceMaterialContextSlots();
    [[nodiscard]] Expected<Optional<Core::GpuUploadBlobId>> retainPreparedCsgTraceContextUpload(Core::GpuTaskGraph& graph)const;
    [[nodiscard]] bool prepareCausticOpticalReadTracking(const ECSRenderDetail::MeshViewBufferSnapshot& meshView);
    void confirmRayTraceContextReadSubmission(const Core::QueueSubmissionToken& token)noexcept;
    // Retain the exact preflight-gathered caustic AABB stream as an immutable graph blob. A valid empty result authoritatively represents a frame without refractive emission targets.
    [[nodiscard]] Expected<Optional<Core::GpuUploadBlobId>> retainPreparedCausticEmissionTargetUpload(Core::GpuTaskGraph& graph)const;
    // Retain the exact per-frame surfel constant payload before graph recording. A valid empty result represents an inactive surfel frame
    [[nodiscard]] Expected<Optional<Core::GpuUploadBlobId>> retainPreparedSurfelFrameConstantsUpload(Core::GpuTaskGraph& graph, const DeferredFrameTargets& targets)const;
    // The material table, instance stream, and typed bytes share indices and offsets. A fresh context retains one all-or-nothing upload batch
    [[nodiscard]] Expected<PreparedShadowMaterialContextUploads> retainPreparedShadowMaterialContextUploads(Core::GpuTaskGraph& graph)const;
    void confirmPreparedShadowMaterialContextUploads()noexcept;
    // The software scene hierarchy and its leaf instances share topology and leaf indices; keep them as one preflight batch and publish both only when the Shadow Preparation packet submits.
    [[nodiscard]] Expected<PreparedSceneBvhUploads> retainPreparedSceneBvhUploads(Core::GpuTaskGraph& graph)const;
    void confirmPreparedSceneBvhUploads()noexcept;
    // Hardware TLAS work records from this frozen preflight plan in Shadow Preparation. Its static cache becomes valid only after that packet accepts.
    [[nodiscard]] bool preparedSceneTlasBuildReady()const noexcept{ return m_preparedSceneTlas.ready(); }
    // Only newly allocated backing has a descriptor-native source; graph imports observe its current accepted generation.
    [[nodiscard]] Core::ResourceStates::Mask sceneTlasBackingInitialState()const noexcept;
    void confirmPreparedSceneTlasBuild()noexcept;
    // Publish acceleration-structure handoffs only after the Shadow Preparation packet accepts.
    // Hardware BLAS build/refit choices retain their selected handles through recording.
    // Only Shadow Preparation acceptance publishes a frozen plan's mesh-cache progress.
    [[nodiscard]] bool preparedMeshBlasBuildsReady()const noexcept{ return m_preparedMeshBlasBuildsReady; }

    [[nodiscard]] const PreparedMeshBlasBuildVector& preparedMeshBlasBuilds()const noexcept{ return m_preparedMeshBlasBuilds; }
    void confirmPreparedMeshBlasBuilds();
    // Software frames freeze selected build/refit work against its shared scratch generation; scene and material snapshots remain independently graph-owned.
    [[nodiscard]] const SoftwareSceneRefitHandle& preparedSceneSwBvhRefit()const noexcept{ return m_preparedSceneSwBvhRefit; }

    [[nodiscard]] bool preparedMeshSwBvhBuildPlanFrozen()const noexcept{ return m_preparedMeshSwBvhBuildPlanFrozen; }

    [[nodiscard]] bool preparedMeshSwBvhBuildsReady()const noexcept{ return m_preparedMeshSwBvhBuildsReady; }

    [[nodiscard]] const PreparedMeshSwBvhBuildVector& preparedMeshSwBvhBuilds()const noexcept{ return m_preparedMeshSwBvhBuilds; }
    // The pure-software Shadow Preparation packet records each frozen build after graph-owned typed sentinel clears. Revalidate this snapshot immediately before its native compute sequence
    [[nodiscard]] bool recordPreparedMeshSwBvhBuildAfterGraphClears(Core::CommandList& commandList, const PreparedMeshSwBvhBuild& build);
    void confirmPreparedMeshSwBvhBuilds();
    void releaseRayTraceMaterialContextHeapHandles();
    void releaseSwBvhScratchHeapHandles();
    void releaseSurfelGiHeapHandles();
    // The deferred graph declares hardware trace entry resources before recording.
    [[nodiscard]] Expected<u32> renderShadowVisibility(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        const DeferredLightingGraphResources& deferredLightingResources,
        bool splitSoftTransparentFold = false,
        bool graphOwnsOpaqueTemporalMergeEntryStates = false,
        bool splitOpaqueSoftResolve = false,
        const LightSpaceShadowSnapshot* lightSpace = nullptr
    );
    [[nodiscard]] Expected<Core::GpuTaskId> declareShadowVisibilityTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        DeferredFrameTargets& targets,
        const DeferredLightingGraphResources& deferredLightingResources,
        const bool& prepared,
        bool hardwareShadowSupported,
        Core::GpuTimingSubmissionTicket& timingTicket,
        GraphOwnedAdaptiveShadowPlan graphOwnedAdaptivePlan = {}
    );
    // A prepared soft-transparent frame splits opaque visibility and transparent fold in one semantic packet. The opaque task opens the shared timing scopes
    [[nodiscard]] Expected<Core::GpuTaskId> declareShadowVisibilityOpaqueTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        DeferredFrameTargets& targets,
        const DeferredLightingGraphResources& deferredLightingResources,
        const bool& prepared,
        bool hardwareShadowSupported,
        Core::GpuTimingSubmissionTicket& timingTicket,
        Optional<Core::GpuTimingMeasure>& asyncTiming,
        Optional<Core::GpuTimingMeasure>& shadowVisibilityTiming,
        bool& opaqueProduced,
        u32& opaqueFrameIndex,
        bool graphOwnsOpaqueTemporalMergeEntryStates = false,
        const LightSpaceShadowSnapshot* lightSpace = nullptr
    );
    // The prepared opaque producer records the trace and geometry downsample first.
    // This adjacent callback owns the compiler-lowered trace/geometry sampled handoff before temporal merge and the first wavelet
    [[nodiscard]] Expected<Core::GpuTaskId> declareShadowVisibilityOpaqueFirstWaveletTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        DeferredFrameTargets& targets,
        const DeferredLightingGraphResources& deferredLightingResources,
        Core::GpuTimingSubmissionTicket& timingTicket,
        Optional<Core::GpuTimingMeasure>& asyncTiming,
        Optional<Core::GpuTimingMeasure>& shadowVisibilityTiming,
        Optional<Core::GpuTimingMeasure>& opaqueResolveTiming,
        bool& opaqueProduced,
        const u32& opaqueFrameIndex,
        bool hardwareShadowSupported,
        bool graphOwnsOpaqueTemporalMergeEntryStates = false,
        bool deferUpsample = false,
        bool deferWavelet = false
    );
    [[nodiscard]] Expected<Core::GpuTaskId> declareShadowVisibilityOpaqueResolveTailTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        DeferredFrameTargets& targets,
        const DeferredLightingGraphResources& deferredLightingResources,
        Core::GpuTimingSubmissionTicket& timingTicket,
        Optional<Core::GpuTimingMeasure>& asyncTiming,
        Optional<Core::GpuTimingMeasure>& shadowVisibilityTiming,
        Optional<Core::GpuTimingMeasure>& opaqueResolveTiming,
        bool& opaqueProduced,
        const u32& opaqueFrameIndex,
        bool hardwareShadowSupported
    );
    // The prepared transparent temporal merge receives frozen history/moment entry states and starts the resolve timing interval.
    // Its output reaches the first wavelet through the compiler-owned task handoff.
    [[nodiscard]] Expected<Core::GpuTaskId> declareShadowTransparentSoftTemporalMergeTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        DeferredFrameTargets& targets,
        const DeferredLightingGraphResources& deferredLightingResources,
        Core::GpuTimingSubmissionTicket& timingTicket,
        Optional<Core::GpuTimingMeasure>& transparentResolveTiming,
        const bool& opaqueProduced,
        bool& transparentTraceProduced,
        const u32& opaqueFrameIndex,
        bool graphOwnsTransparentTemporalMergeEntryStates = false,
        bool combinedTemporal = false,
        bool hardwareShadowSupported = false
    );
    // The prepared transparent trace or temporal merge's first wavelet inherits its graph-declared input and output states.
    // The terminal fold remains responsible for the final upsample, submission acceptance, and history publication.
    [[nodiscard]] Expected<Core::GpuTaskId> declareShadowTransparentSoftFirstWaveletTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        DeferredFrameTargets& targets,
        const DeferredLightingGraphResources& deferredLightingResources,
        Core::GpuTimingSubmissionTicket& timingTicket,
        Optional<Core::GpuTimingMeasure>& transparentResolveTiming,
        const bool& opaqueProduced,
        bool& transparentTraceProduced,
        const u32& opaqueFrameIndex,
        bool graphOwnsTransparentWaveletInputBoundary = false,
        bool startsTransparentResolveTiming = true,
        bool combinedWavelet = false
    );
    [[nodiscard]] Expected<Core::GpuTaskId> declareShadowTransparentSoftFoldTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        DeferredFrameTargets& targets,
        const DeferredLightingGraphResources& deferredLightingResources,
        Core::GpuTimingSubmissionTicket& timingTicket,
        Optional<Core::GpuTimingMeasure>& asyncTiming,
        Optional<Core::GpuTimingMeasure>& shadowVisibilityTiming,
        Optional<Core::GpuTimingMeasure>& transparentResolveTiming,
        const bool& opaqueProduced,
        bool& transparentTraceProduced,
        const u32& opaqueFrameIndex,
        bool combinedUpsample = false
    );
    [[nodiscard]] Expected<Core::GpuTaskId> declareShadowTransparentSoftTraceTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        DeferredFrameTargets& targets,
        const DeferredLightingGraphResources& deferredLightingResources,
        Core::GpuTimingSubmissionTicket& timingTicket,
        const bool& opaqueProduced,
        const u32& opaqueFrameIndex,
        bool& transparentTraceProduced,
        const LightSpaceShadowSnapshot* lightSpace = nullptr
    );
    void clearShadowVisibility(Core::CommandList& commandList, DeferredFrameTargets& targets);
    void confirmCausticAccumulatorNonTemporalClear()noexcept;
    // The temporal bootstrap clear is recorded by a graph task, but this mirror changes only when the containing caustic producer packet accepts.
    void confirmCausticAccumulatorBootstrapClear()noexcept;
    // A warm temporal accumulator decays in its own graph task before the selected photon producer. The task shares the producer packet, so the compiler owns the UAV handoff between dispatches.
    [[nodiscard]] Expected<Core::GpuTaskId> declareCausticAccumulatorDecayTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        DeferredFrameTargets& targets,
        const ECSRenderDetail::MeshViewBufferSnapshot& meshView,
        const bool& shadowVisibilityPrepared,
        f32 decayFactor,
        bool hardwareCaustics,
        Core::GpuTimingSubmissionTicket& timingTicket,
        Optional<Core::GpuTimingMeasure>& causticPhotonTiming
    );
    // Record the decay dispatch itself.  Graph callers leave entry state lowering and the following producer's UAV dependency to the compiler
    [[nodiscard]] bool dispatchCausticAccumulatorDecay(Core::CommandList& commandList, DeferredFrameTargets& targets, f32 decayFactor);
    // Software visibility consumes graph-declared traversal inputs for opaque shadows and transparent transmittance.
    [[nodiscard]] Expected<u32> renderGpuBvhShadowVisibility(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        const DeferredLightingGraphResources& deferredLightingResources,
        bool splitSoftTransparentFold = false,
        bool graphOwnsOpaqueTemporalMergeEntryStates = false,
        bool splitOpaqueSoftResolve = false,
        const GraphOwnedAdaptiveShadowPlan* graphOwnedAdaptivePlan = nullptr,
        const LightSpaceShadowSnapshot* lightSpace = nullptr
    );
    [[nodiscard]] bool prepareGpuBvhCausticResources(DeferredFrameTargets& targets);
    [[nodiscard]] Expected<Core::GpuTaskId> declareSoftwareCausticsTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        DeferredFrameTargets& targets,
        const DeferredLightingGraphResources& deferredLightingResources,
        const ECSRenderDetail::MeshViewBufferSnapshot& meshView,
        const bool& shadowVisibilityPrepared,
        Core::GpuTimingSubmissionTicket& timingTicket,
        Optional<Core::GpuTimingMeasure>& causticPhotonTiming,
        bool& causticProducerDispatched,
        bool graphOwnsAccumulatorBootstrapClear = false,
        bool graphOwnsNonTemporalAccumulatorClear = false
    );
    [[nodiscard]] bool hasCausticWork(const ECSRenderDetail::MeshViewBufferSnapshot& meshView)const noexcept;
    [[nodiscard]] bool renderGpuBvhCaustics(Core::CommandList& commandList, const ECSRenderDetail::MeshViewBufferSnapshot& meshView, DeferredFrameTargets& targets, Optional<Core::GpuTimingMeasure>* causticPhotonTiming = nullptr);
    [[nodiscard]] bool prepareHwCausticResources(DeferredFrameTargets& targets);
    [[nodiscard]] Expected<Core::GpuTaskId> declareHardwareCausticsTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        DeferredFrameTargets& targets,
        const DeferredLightingGraphResources& deferredLightingResources,
        const ECSRenderDetail::MeshViewBufferSnapshot& meshView,
        const bool& shadowVisibilityPrepared,
        Core::GpuTimingSubmissionTicket& timingTicket,
        Optional<Core::GpuTimingMeasure>& causticPhotonTiming,
        bool& causticProducerDispatched,
        bool graphOwnsAccumulatorBootstrapClear = false,
        bool graphOwnsNonTemporalAccumulatorClear = false
    );
    [[nodiscard]] bool renderHwCaustics(Core::CommandList& commandList, const ECSRenderDetail::MeshViewBufferSnapshot& meshView, DeferredFrameTargets& targets, Optional<Core::GpuTimingMeasure>* causticPhotonTiming = nullptr);
    // The normal deferred graph records downsample, resolve prepare, wavelet passes, and upsample after the photon producer. Their uses own the immutable and ping-pong UAV-to-SRV handoffs
    [[nodiscard]] Expected<Core::GpuTaskId> declareCausticGeometryDownsampleTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        DeferredFrameTargets& targets,
        Core::GpuTimingSubmissionTicket& timingTicket,
        const bool& causticProducerDispatched,
        Optional<Core::GpuTimingMeasure>& causticResolveTiming
    );
    [[nodiscard]] CausticResolveActivitySnapshot causticResolveActivitySnapshot(const DeferredFrameTargets& targets)const noexcept;
    [[nodiscard]] Expected<Core::GpuTaskId> declareCausticResolveTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        Core::GpuTimingSubmissionTicket& timingTicket,
        const bool& causticProducerDispatched,
        Optional<Core::GpuTimingMeasure>& causticResolveTiming
    );
    [[nodiscard]] Expected<Core::GpuTaskId> declareCausticResolvePrepareTask(Core::GpuTaskGraph& graph, const Core::GpuTaskDesc& desc, DeferredFrameTargets& targets, const bool& causticProducerDispatched);
    [[nodiscard]] Expected<Core::GpuTaskId> declareCausticResolveWaveletTask(Core::GpuTaskGraph& graph, const Core::GpuTaskDesc& desc, DeferredFrameTargets& targets, const bool& causticProducerDispatched);
    [[nodiscard]] Expected<Core::GpuTaskId> declareCausticResolveSecondWaveletTask(Core::GpuTaskGraph& graph, const Core::GpuTaskDesc& desc, DeferredFrameTargets& targets, const bool& causticProducerDispatched);
    [[nodiscard]] Expected<Core::GpuTaskId> declareCausticResolveThirdWaveletTask(Core::GpuTaskGraph& graph, const Core::GpuTaskDesc& desc, DeferredFrameTargets& targets, const bool& causticProducerDispatched);
    [[nodiscard]] Expected<Core::GpuTaskId> declareCausticResolveFourthWaveletTask(Core::GpuTaskGraph& graph, const Core::GpuTaskDesc& desc, DeferredFrameTargets& targets, const bool& causticProducerDispatched);
    [[nodiscard]] Expected<Core::GpuTaskId> declareCausticResolveFifthWaveletTask(Core::GpuTaskGraph& graph, const Core::GpuTaskDesc& desc, DeferredFrameTargets& targets, const bool& causticProducerDispatched);
    [[nodiscard]] Expected<Core::GpuTaskId> declareCausticResolveUpsampleTask(Core::GpuTaskGraph& graph, const Core::GpuTaskDesc& desc, DeferredFrameTargets& targets, const bool& causticProducerDispatched);
    // Typed stages own immutable inputs and each ping-pong handoff.
    void dispatchCausticGeometryDownsample(Core::CommandList& commandList, DeferredFrameTargets& targets);
    void dispatchCausticResolvePrepare(Core::CommandList& commandList, DeferredFrameTargets& targets);
    void dispatchCausticResolveWaveletPass(Core::CommandList& commandList, DeferredFrameTargets& targets, u32 passIndex);
    void dispatchCausticResolveUpsample(Core::CommandList& commandList, DeferredFrameTargets& targets);
    [[nodiscard]] bool hasHwCausticWork()const noexcept;
    [[nodiscard]] bool hasHwCausticWork(const ECSRenderDetail::MeshViewBufferSnapshot& meshView)const noexcept;
    [[nodiscard]] bool hasSurfelWork()const noexcept;
    [[nodiscard]] bool needsSurfelResourceInitialization()const noexcept;
    // Typed graph clear primitives own the persistent-buffer writes. This resource-free task only records and publishes their CPU lifecycle after the shared producer packet has accepted.
    [[nodiscard]] Expected<Core::GpuTaskId> declareSurfelResourceInitializationLifecycleTask(Core::GpuTaskGraph& graph, const Core::GpuTaskDesc& desc);
    [[nodiscard]] bool recordSurfelResourceInitializationLifecycle()noexcept;
    // The initialization mirror commits only after the typed-clear producer packet accepts.
    void finalizeSurfelResourceInitialization()noexcept;
    void discardSurfelResourceInitialization()noexcept;
    // Persistent surfel storage survives resize.
    [[nodiscard]] bool ensureSurfelResources();
    [[nodiscard]] bool ensureSurfelSpawnPipeline();
    [[nodiscard]] bool ensureSurfelAgeFreePipeline();
    [[nodiscard]] bool ensureSurfelHashBuildPipeline();
    [[nodiscard]] bool ensureSurfelTracePipeline();
    // Hardware surfel trace uses inline RayQuery over the TLAS.
    [[nodiscard]] bool ensureSurfelTraceHwPipeline();
    // Resolve keeps the writable surfel pool off deferred lighting.
    [[nodiscard]] bool ensureSurfelResolvePipeline();
    [[nodiscard]] bool ensureSurfelUpsamplePipeline();
    [[nodiscard]] bool ensureSurfelTraceBuildArgsPipeline();
    // The graph schedules the small diagnostic readback as a late copy task and publishes its token.
    [[nodiscard]] bool shouldCaptureSurfelCountReadback()const noexcept;
    void markSurfelCountReadbackScheduled()noexcept;
    [[nodiscard]] bool softTransparentShadowReady()const noexcept;
    // Commit soft-shadow history only after ordered submission accepts.
    void finalizeSoftShadowTemporalHistory(DeferredFrameTargets& targets)noexcept;
    void discardSoftShadowTemporalHistory()noexcept;


private:
    [[nodiscard]] bool preparePendingMeshBlasResources(Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] bool prepareSceneTlasResources(Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] bool prepareSceneSwBvhResources(Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] bool prepareCausticEmissionTargetResources(Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] bool capturePreparedShadowMaterialContext(
        PreparedShadowMaterialContextRoute::Enum route,
        bool staticScene,
        u64 hash,
        const void* instanceMaterialData,
        usize instanceMaterialCount,
        usize instanceMaterialByteCount,
        const void* instanceData,
        usize instanceCount,
        usize instanceByteCount,
        const void* materialTypedData,
        usize materialTypedByteCount
    );
    [[nodiscard]] bool preparedTraversalStorageMatchesCurrent()const noexcept;
    [[nodiscard]] bool capturePreparedShadowMaterialContextCacheReuse(u64 hash, usize instanceMaterialCount, usize instanceCount, usize materialTypedByteCount)noexcept;
    void clearPreparedShadowMaterialContext()noexcept;
    [[nodiscard]] bool appendPreparedShadowTraceMaterialSampledTextures(const MaterialSurfaceInfo& materialInfo, ShadowMaterialSampledTextureCollector& collector);
    void clearPreparedShadowTraceMaterialSampledTextures()noexcept;
    [[nodiscard]] bool capturePreparedSceneBvh(bool staticScene, u64 staticSceneHash, const void* nodeData, usize nodeCount, usize nodeByteCount, const void* instanceData, usize instanceCount, usize instanceByteCount);
    [[nodiscard]] bool capturePreparedSceneBvhCacheReuse(u64 staticSceneHash, u32 instanceCount)noexcept;
    void clearPreparedSceneBvh()noexcept;
    [[nodiscard]] bool capturePreparedSceneSwBvhTraversal(const PreparedSceneSwBvhMesh* meshes, usize meshCount, u32 instanceCount);
    // Preflight leaves the exact software descriptor tables populated. The frozen traversal validates those tables and retained resource identities without republishing mutable state.
    [[nodiscard]] bool recordPreparedSceneSwBvhTraversal();
    void clearPreparedSceneSwBvhTraversal()noexcept;
    [[nodiscard]] bool capturePreparedMeshBlasBuilds(Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] bool recordPreparedMeshBlasBuilds(Core::CommandList& commandList, bool meshBlasGeometryBuildInputStatesGraphOwned);
    void clearPreparedMeshBlasBuilds()noexcept;
    [[nodiscard]] bool capturePreparedMeshSwBvhBuilds(Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] bool preparedMeshSwBvhBuildMatchesCurrent(const PreparedMeshSwBvhBuild& build)noexcept;
    [[nodiscard]] bool preparedMeshSwBvhBuildProducesTopology(const ECSRenderDetail::MeshRayTracingResourceSnapshot& mesh)const noexcept;
    void clearPreparedMeshSwBvhBuilds()noexcept;
    [[nodiscard]] bool prepareSurfelResources(DeferredFrameTargets& targets);
    [[nodiscard]] bool renderSurfelGiAgeFree(Core::CommandList& commandList, DeferredFrameTargets& targets, const DeferredLightingGraphResources& deferredLightingResources);
    [[nodiscard]] bool renderSurfelGiUpsample(Core::CommandList& commandList, DeferredFrameTargets& targets, const DeferredLightingGraphResources& deferredLightingResources);
    [[nodiscard]] bool renderSurfelGiHashBuild(Core::CommandList& commandList, DeferredFrameTargets& targets, const DeferredLightingGraphResources& deferredLightingResources);
    [[nodiscard]] bool renderSurfelGiSpawn(Core::CommandList& commandList, DeferredFrameTargets& targets, const DeferredLightingGraphResources& deferredLightingResources);
    [[nodiscard]] bool renderSurfelGiTraceBuildArgs(Core::CommandList& commandList, DeferredFrameTargets& targets, const DeferredLightingGraphResources& deferredLightingResources);
    [[nodiscard]] bool renderSurfelGiTrace(Core::CommandList& commandList, DeferredFrameTargets& targets, const DeferredLightingGraphResources& deferredLightingResources);
    [[nodiscard]] bool renderSurfelGiResolve(Core::CommandList& commandList, DeferredFrameTargets& targets, const DeferredLightingGraphResources& deferredLightingResources);
    [[nodiscard]] bool renderSurfelGiPhases(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        const DeferredLightingGraphResources& deferredLightingResources,
        bool dispatchAgeFree,
        bool dispatchHashBuild,
        bool dispatchSpawn,
        bool dispatchTraceBuildArgs,
        bool dispatchTrace,
        bool dispatchResolve,
        bool dispatchRemaining
    );
    [[nodiscard]] bool prepareMeshBlasResources(ECSRenderDetail::MeshRayTracingResourceSnapshot& meshResources);
    // Runtime meshes prepare every frame; static meshes remain dirty until first build.
    [[nodiscard]] bool preparePendingMeshSwBvhResources(Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] bool ensureShadowPipeline();
    // Hardware soft trace feeds the shared software denoise chain.
    [[nodiscard]] bool ensureShadowSoftPipeline();
    // Hardware shadow layouts are push-only; resources come from heap sets.
    void appendShadowTraceBindingLayout(Core::BindingLayoutDesc& layoutDesc)const;
    [[nodiscard]] bool ensureSwShadowPipeline();
    [[nodiscard]] bool ensureSwShadowPassPipeline(Core::ShaderHandle& shader, Core::ComputePipelineHandle& pipeline, const Name& shaderName, const NameText<char> debugLabel);
    [[nodiscard]] bool ensureSoftwareTransparentSamplingPipeline();
    [[nodiscard]] bool ensureSoftShadowResolvePipeline();
    [[nodiscard]] bool ensureShadowGeometryDownsamplePipeline();
    [[nodiscard]] bool ensureSoftTransparentResolvePipeline();
    void prepareSoftCombinedResolvePipelines();
    [[nodiscard]] bool ensureSoftCombinedUpsamplePipeline();
    [[nodiscard]] bool ensureSoftCombinedWaveletPipeline();
    [[nodiscard]] bool ensureSoftCombinedTemporalPipeline();
    [[nodiscard]] bool renderSoftShadowCombinedTemporalMerge(Core::CommandList& commandList, DeferredFrameTargets& targets);
    [[nodiscard]] bool renderSoftShadowCombinedWavelet(Core::CommandList& commandList, DeferredFrameTargets& targets);
    [[nodiscard]] bool renderSoftShadowTerminalUpsample(Core::CommandList& commandList, DeferredFrameTargets& targets, const DeferredLightingGraphResources& deferredLightingResources, bool transparentReady);

    [[nodiscard]] bool prepareHardwareTransparentShadowResources(DeferredFrameTargets& targets);
    void releaseHardwareTransparentShadowResources();
    [[nodiscard]] bool hardwareTransparentShadowReady()const noexcept;
    void dispatchHardwareTransparentShadow(Core::CommandList& commandList, DeferredFrameTargets& targets, u32 frameIndex);

    // Resolve a contiguous shadow-slot range in one heap-selected dispatch.
    void dispatchSoftShadowResolve(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        const DeferredLightingGraphResources& deferredLightingResources,
        u32 slotStart,
        u32 slotCount,
        const SoftShadowResolveDispatch& dispatch,
        bool dispatchFirstWavelet = true,
        bool dispatchTail = true
    );
    void enableSoftShadowResolveBarriers(Core::CommandList& commandList, DeferredFrameTargets& targets);
    void dispatchPrimarySoftShadowResolve(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        const DeferredLightingGraphResources& deferredLightingResources,
        u32 frameIndex,
        u32 softGroupsX,
        u32 softGroupsY,
        bool splitOpaqueSoftResolve,
        bool splitSoftTransparentFold,
        bool graphOwnsOpaqueTemporalMergeEntryStates
    );
    // Denoise either backend's soft trace and optionally run the transparent trace and resolve phases.
    // The shared deferred graph supplies the first downsample entry states, the opaque-to-resolve handoff, and prepared transparent trace-to-merge/first-wavelet plus handoffs; later transitions stay local.
    void dispatchSoftShadowDenoiseAndTransparentFold(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        const DeferredLightingGraphResources& deferredLightingResources,
        u32 frameIndex,
        u32 softGroupsX,
        u32 softGroupsY,
        bool dispatchOpaqueGeometry = true,
        bool dispatchOpaqueResolve = true,
        bool dispatchTransparentTrace = true,
        bool dispatchTransparentResolve = true,
        bool graphOwnsOpaqueGeometryToResolveBoundary = false,
        bool graphOwnsOpaqueToTransparentBoundary = false,
        bool graphOwnsTransparentTraceToResolveBoundary = false,
        bool graphOwnsOpaqueTemporalMergeEntryStates = false,
        bool graphOwnsTransparentTemporalMergeEntryStates = false,
        bool dispatchOpaqueResolveTail = true,
        bool graphOwnsOpaqueTraceToFirstWaveletBoundary = false,
        bool dispatchTransparentResolveTail = false,
        bool splitTransparentResolve = false,
        bool dispatchTransparentTemporalMerge = false,
        SoftShadowOpaqueResolvePhase::Enum opaquePhase = SoftShadowOpaqueResolvePhase::TemporalAndWavelet
    );
    // Graph-only phase helpers preserve the complete direct route above while exposing both in-packet handoffs to the shared deferred graph.
    [[nodiscard]] Expected<u32> renderShadowVisibilityOpaque(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        const DeferredLightingGraphResources& deferredLightingResources,
        bool graphOwnsOpaqueTemporalMergeEntryStates,
        const LightSpaceShadowSnapshot* lightSpace
    );
    [[nodiscard]] bool renderSoftOpaqueShadowResolvePhase(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        const DeferredLightingGraphResources& deferredLightingResources,
        u32 frameIndex,
        bool hardwareShadowSupported,
        bool graphOwnsOpaqueTemporalMergeEntryStates,
        SoftShadowOpaqueResolvePhase::Enum phase
    );
    [[nodiscard]] bool renderSoftOpaqueShadowResolveTail(Core::CommandList& commandList, DeferredFrameTargets& targets, const DeferredLightingGraphResources& deferredLightingResources, u32 frameIndex, bool hardwareShadowSupported);
    [[nodiscard]] Expected<u32> renderGpuBvhShadowVisibilityOpaque(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        const DeferredLightingGraphResources& deferredLightingResources,
        bool graphOwnsOpaqueTemporalMergeEntryStates,
        const LightSpaceShadowSnapshot* lightSpace = nullptr
    );
    [[nodiscard]] bool renderSoftTransparentShadowTrace(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        const DeferredLightingGraphResources& deferredLightingResources,
        u32 frameIndex,
        bool graphOwnsOpaqueToTransparentBoundary,
        const LightSpaceShadowSnapshot* lightSpace = nullptr
    );
    void reportSoftwareShadowTraversal(const DeferredFrameTargets& targets);
    [[nodiscard]] bool renderSoftTransparentShadowTemporalMerge(Core::CommandList& commandList, DeferredFrameTargets& targets, const DeferredLightingGraphResources& deferredLightingResources, u32 frameIndex, bool graphOwnsTransparentTemporalMergeEntryStates);
    [[nodiscard]] bool renderSoftTransparentShadowFirstWavelet(Core::CommandList& commandList, DeferredFrameTargets& targets, const DeferredLightingGraphResources& deferredLightingResources, u32 frameIndex, bool graphOwnsTransparentWaveletInputBoundary);
    [[nodiscard]] bool renderSoftTransparentShadowFold(Core::CommandList& commandList, DeferredFrameTargets& targets, const DeferredLightingGraphResources& deferredLightingResources, u32 frameIndex);
    // Temporal merge precedes soft resolve and swaps history at frame end.
    [[nodiscard]] bool ensureShadowReprojectMergePipeline();
    [[nodiscard]] bool softShadowTemporalHistoryUsable()const noexcept;
    [[nodiscard]] u32 transparentShadowSampleCount()const noexcept;
    void reportTransparentShadowSampling(u32 sampleCount);
    void swapSoftShadowTemporalHistory(DeferredFrameTargets& targets)noexcept;
    [[nodiscard]] bool prepareCausticResources(const ECSRenderDetail::MeshViewBufferSnapshot& meshView, DeferredFrameTargets& targets, bool hardware);
    [[nodiscard]] bool ensureSwCausticPipeline();
    [[nodiscard]] bool ensureCausticResolvePipeline();
    [[nodiscard]] bool prepareCausticResolveActivity(u32 halfWidth, u32 halfHeight);
    void releaseCausticResolveActivity();
    [[nodiscard]] bool ensureCausticGeometryDownsamplePipeline();
    [[nodiscard]] bool causticResolveResourcesReady(const DeferredFrameTargets& targets, f32 temporalDecay)const noexcept;
    [[nodiscard]] bool ensureCausticAccumulatorDecayPipeline();
    [[nodiscard]] bool ensureCausticRtPipeline();
    [[nodiscard]] bool ensureBvhSortPipeline();
    [[nodiscard]] bool ensureBvhSortBuffers(usize paddedCount);
    [[nodiscard]] bool bvhBitonicSort(Core::CommandList& commandList, u32 elementCount, u32 paddedCount);
    [[nodiscard]] bool ensureBvhBuildPipeline();
    [[nodiscard]] bool ensureBvhVisitCounterBuffer(usize primitiveCount);
    [[nodiscard]] bool createMeshBvhStorage(usize primitiveCount, Core::BufferHandle& nodeBuffer, Core::BufferHandle& parentBuffer, Core::GpuDescriptorHandle& nodeHeapHandle, Core::GpuDescriptorHandle& parentHeapHandle);
    [[nodiscard]] bool ensureMeshSwBvhResources(u32 primitiveCount, Core::BufferHandle& nodeBuffer, Core::BufferHandle& parentBuffer, Core::GpuDescriptorHandle& nodeHeapHandle, Core::GpuDescriptorHandle& parentHeapHandle);
    [[nodiscard]] bool meshSwBvhResourcesReady(const Core::BufferHandle& nodeBuffer, const Core::BufferHandle& parentBuffer, Core::GpuDescriptorHandle nodeHeapHandle, Core::GpuDescriptorHandle parentHeapHandle)noexcept;
    [[nodiscard]] bool buildMeshSwBvhPrepared(
        Core::CommandList& commandList, u32 positionHeapSlot, u32 triangleIndexHeapSlot, u32 primitiveCount, const Float3Int& aabbMin, const Float3Int& aabbMax, Core::BufferHandle& nodeBuffer, Core::BufferHandle& parentBuffer,
        Core::GpuDescriptorHandle nodeHeapHandle, Core::GpuDescriptorHandle parentHeapHandle
    );
    [[nodiscard]] bool refitMeshSwBvhPrepared(
        Core::CommandList& commandList, u32 positionHeapSlot, u32 triangleIndexHeapSlot, u32 primitiveCount, Core::BufferHandle& nodeBuffer, Core::BufferHandle& parentBuffer, Core::GpuDescriptorHandle nodeHeapHandle, Core::GpuDescriptorHandle parentHeapHandle
    );
    void preflightLightSpaceShadowResources();
    [[nodiscard]] Expected<LightSpacePlan> buildLightSpaceShadowPlan(const ECSRenderDetail::SceneLightGpuData* lights, u32 lightCount)const noexcept;
    void prepareLightSpaceShadows(const ECSRenderDetail::SceneLightGpuData* lights, u32 lightCount);
    [[nodiscard]] bool ensureLightSpaceShadowPipelines();
    [[nodiscard]] bool ensureLightSpaceShadowStorage(const LightSpacePlan& plan);
    void releaseLightSpaceShadowResources();
    [[nodiscard]] bool ensureSceneBvhBuffers(u32 instanceCount);
    [[nodiscard]] bool ensureRayTraceMaterialContextSlotsBuffer();
    [[nodiscard]] bool ensureRayTraceContextReadTracking(bool opticalCausticReader);
    [[nodiscard]] bool ensureCsgTraceContextResources();
    [[nodiscard]] bool ensureRayTraceMaterialContextSlotsHeapHandle();
    [[nodiscard]] bool ensureRayTraceMaterialContextHeapHandle(Core::Buffer& buffer, Core::GpuDescriptorHandle& handle);
    [[nodiscard]] bool replaceRayTraceMaterialContextHeapHandle(Core::Buffer& buffer, Core::GpuDescriptorHandle& handle);
    [[nodiscard]] bool ensureCausticEmissionTargetBuffer(usize targetCount);
    [[nodiscard]] bool ensureShadowInstanceMaterialBuffer(usize instanceCount);
    [[nodiscard]] bool ensureShadowInstanceContextBuffer(usize instanceCount);
    [[nodiscard]] bool ensureShadowMaterialTypedBuffer(usize byteCount);


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
    // Fresh graph uploads and accepted static-cache reuse both freeze the matching traversal table. Recording never rebuilds CPU scene data; ECS mutation versions reject stale frozen plans.
    PreparedSceneSwBvhMeshVector m_preparedSceneSwBvhMeshes;
    u64 m_preparedSceneSwBvhRendererMutationVersion = 0u;
    u64 m_preparedSceneSwBvhTransformMutationVersion = 0u;
    u64 m_preparedSceneSwBvhMaterialMutationVersion = 0u;
    PreparedMeshBlasBuildVector m_preparedMeshBlasBuilds;
    // Per-frame BLAS actions keyed by dirty reason.
    // Answers whether the stress scene rebuilds per-mesh geometry when only instance transforms change.
    // Logged on confirm
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
    bool m_temporalOneShadowSamplingLogged = false;
    bool m_preparedSceneSwBvhReady = false;
    bool m_preparedMeshBlasBuildsReady = false;
    bool m_preparedMeshSwBvhBuildsReady = false;
    bool m_preparedMeshSwBvhBuildPlanFrozen = false;
    bool m_shadowVisibilityResourcesPreflighted = false;
    bool m_shadowVisibilityHardwareSupported = false;
    bool m_opticalCausticReaderAdmitted = false;
    // Dedupe for the refractive-volume optical-boundary diagnostic: count, hash, first entity, reporting latch.
    u32 m_unsupportedCausticVolumeCount = 0u;
    u64 m_unsupportedCausticVolumeHash = 0u;
    u64 m_reportedUnsupportedCausticVolumeHash = 0u;
    u32 m_firstUnsupportedCausticVolumeEntity = 0u;
    bool m_unsupportedCausticVolumeReported = false;
    // Recording may only touch allocations selected before the shared graph is compiled.
    // These flags distinguish a usable frozen trace plan from a non-fatal preflight fallback that leaves the effect black for this frame.
    bool m_shadowVisibilityTraceResourcesPreflighted = false;
    bool m_shadowVisibilityBackendPipelinePreflighted = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

