// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/kernel/subsystem_base.h>

#include <core/alloc/scratch.h>
#include <core/graphics/gpu_timing.h>
#include <core/graphics/task_graph/task_graph.h>
#include <global/simdmath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MaterialSurfaceInfo;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Soft resolve either overwrites visibility or multiplies transparent transmittance.
namespace SoftShadowUpsampleFold{
    enum Enum : u32{
        Overwrite = 0u,
        Multiply = 1u,
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace PreparedShadowTraceGeometryRole{
    inline constexpr u8 HardwarePosition = 1u << 0u;
    inline constexpr u8 HardwareIndex = 1u << 1u;
    inline constexpr u8 HardwareAttribute = 1u << 2u;
    inline constexpr u8 SoftwareNode = 1u << 3u;
    inline constexpr u8 SoftwarePosition = 1u << 4u;
    inline constexpr u8 SoftwareIndex = 1u << 5u;
    inline constexpr u8 SoftwareAttribute = 1u << 6u;
};

struct PreparedShadowTraceGeometryBuffer{
    Core::BufferHandle buffer;
    Name identity;
    Core::ResourceStates::Mask initialState = Core::ResourceStates::Common;
    u8 roles = 0u;
};

using PreparedShadowTraceGeometryBufferVector = Vector<
    PreparedShadowTraceGeometryBuffer,
    Core::Alloc::GlobalArena
>;

// Material surface hooks in the shadow, caustic, and GI trace paths select texture assets through typed bindless
// slots. Keep the exact preflight-resolved handles beside the trace geometry so graph declaration never has to
// inspect mutable material caches while recording.
using PreparedShadowTraceMaterialSampledTextureVector = Vector<
    Core::TextureHandle,
    Core::Alloc::GlobalArena
>;


// A BLAS build derives its geometry directly from retained buffers. Keep the resolved operation rather than a
// MeshResources pointer: runtime meshes can be replaced or pruned between preflight and native recording.
struct PreparedMeshBlasBuild{
    Name meshName = NAME_NONE;
    Core::BufferHandle positionBuffer;
    Core::BufferHandle triangleIndexBuffer;
    Core::RayTracingAccelStructHandle blas;
    Core::BufferHandle blasBackingBuffer;
    u64 runtimeMeshVersion = 0u;
    usize positionByteSize = 0u;
    u32 vertexStride = 0u;
    u32 vertexCount = 0u;
    u32 indexCount = 0u;
    u32 refitsBeforeBuild = 0u;
    u32 refitsAfterBuild = 0u;
    bool runtimeMesh = false;
    bool firstBuild = false;
    bool performRefit = false;
};

using PreparedMeshBlasBuildVector = Vector<
    PreparedMeshBlasBuild,
    Core::Alloc::GlobalArena
>;


// The software mesh builder shares global sort/payload/counter storage, so one frozen operation retains both its
// mesh-local inputs/outputs and the exact shared scratch generation. Never retain MeshResources pointers: runtime
// geometry can be replaced or pruned between preflight and Shadow Preparation recording.
struct PreparedMeshSwBvhBuild{
    Name meshName = NAME_NONE;
    Core::BufferHandle positionBuffer;
    Core::BufferHandle triangleIndexBuffer;
    Core::BufferHandle nodeBuffer;
    Core::BufferHandle parentBuffer;
    Core::BufferHandle sortKeysBuffer;
    Core::BufferHandle sortPayloadBuffer;
    Core::BufferHandle visitCounterBuffer;
    Core::GpuDescriptorHandle positionHeapHandle;
    Core::GpuDescriptorHandle triangleIndexHeapHandle;
    Core::GpuDescriptorHandle nodeHeapHandle;
    Core::GpuDescriptorHandle parentHeapHandle;
    Core::GpuDescriptorHandle sortKeysHeapHandle;
    Core::GpuDescriptorHandle sortPayloadHeapHandle;
    Core::GpuDescriptorHandle visitCounterHeapHandle;
    Float3Int aabbMin;
    Float3Int aabbMax;
    u64 runtimeMeshVersion = 0u;
    usize positionByteSize = 0u;
    usize indexByteSize = 0u;
    usize nodeByteSize = 0u;
    usize parentByteSize = 0u;
    usize sortKeysByteSize = 0u;
    usize sortPayloadByteSize = 0u;
    usize visitCounterByteSize = 0u;
    u32 primitiveCount = 0u;
    u32 refitsBeforeBuild = 0u;
    u32 refitsAfterBuild = 0u;
    bool runtimeMesh = false;
    bool buildPending = false;
    bool firstBuild = false;
    bool performRefit = false;
};

using PreparedMeshSwBvhBuildVector = Vector<
    PreparedMeshSwBvhBuild,
    Core::Alloc::GlobalArena
>;


// The scene-level software traversal consumes one descriptor-table entry per distinct mesh. Retain owning buffer
// handles rather than the mutable raw tables rebuilt by the legacy recording path.
struct PreparedSceneSwBvhMesh{
    Name meshName = NAME_NONE;
    Core::BufferHandle nodeBuffer;
    Core::BufferHandle positionBuffer;
    Core::BufferHandle triangleIndexBuffer;
    Core::BufferHandle attributeBuffer;
    Core::GpuDescriptorHandle nodeHeapHandle;
    Core::GpuDescriptorHandle positionHeapHandle;
    Core::GpuDescriptorHandle triangleIndexHeapHandle;
    Core::GpuDescriptorHandle attributeHeapHandle;
    u64 runtimeMeshVersion = 0u;
    usize nodeByteSize = 0u;
    usize positionByteSize = 0u;
    usize triangleIndexByteSize = 0u;
    usize attributeByteSize = 0u;
    u32 primitiveCount = 0u;
    bool runtimeMesh = false;
};

using PreparedSceneSwBvhMeshVector = Vector<
    PreparedSceneSwBvhMesh,
    Core::Alloc::GlobalArena
>;


namespace RayTracingSurfelGiTaskDetail{
    struct SurfelGiAgeFreeGraphTask;
    struct SurfelGiHashBuildGraphTask;
    struct SurfelGiSpawnGraphTask;
    struct SurfelGiTraceBuildArgsGraphTask;
    struct SurfelGiTraceGraphTask;
    struct SurfelGiResolveGraphTask;
    struct SurfelGiGraphTask;
}

namespace RayTracingShadowVisibilityTaskDetail{
    struct ShadowVisibilityOpaqueGraphTask;
    struct ShadowVisibilityOpaqueFirstWaveletGraphTask;
    struct ShadowVisibilityOpaqueResolveTailGraphTask;
    struct ShadowTransparentSoftTraceGraphTask;
    struct ShadowTransparentSoftTemporalMergeGraphTask;
    struct ShadowTransparentSoftFirstWaveletGraphTask;
    struct ShadowTransparentSoftFoldGraphTask;
    struct ShadowVisibilityGraphTask;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Adaptive software-shadow diagnostics are normally private scratch work.  The shared deferred graph freezes this
// small plan before compilation so its counter/stat clears and optional readback can be declared as first-class
// primitive tasks, while direct compatibility callers retain the native operations.
struct GraphOwnedAdaptiveShadowPrimitivePlan{
    bool enabled = false;
    bool compact = false;
    bool captureStatsSnapshot = false;
    u32 statsTick = 0u;
    bool* adaptiveRouteRecorded = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererRayTracingSystem final : public RendererSystemSubsystemBase<RendererSystem>{
public:
    explicit RendererRayTracingSystem(RendererSystem& renderer);
    ~RendererRayTracingSystem();


public:
    void logCapabilityOnce();

    // Resource identity is frozen by the shared deferred graph.  Select/grow every trace resource before graph
    // compilation, then let the graph-owned preparation packet issue only GPU work against that frozen set.
    [[nodiscard]] bool preflightShadowVisibilityResources(DeferredFrameTargets& targets, Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] bool recordPreflightShadowVisibilityResources(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        bool& outBackendReady,
        bool causticEmissionTargetsGraphOwned = false,
        bool surfelFrameConstantsGraphOwned = false,
        bool shadowMaterialContextBatchGraphOwned = false,
        bool sceneBvhBatchGraphOwned = false,
        bool sceneTlasBuildGraphOwned = false,
        bool meshBlasBuildsGraphOwned = false,
        bool meshBlasGeometryBuildInputStatesGraphOwned = false,
        bool meshSwBvhBuildsGraphOwned = false,
        bool preparedMeshSwBvhBuildsRecordedByGraph = false,
        bool deferHybridSoftwareTail = false
    );
    // The hybrid HW-to-SW continuation stays in the accepting Shadow Preparation packet, but records after the
    // frozen hardware build so its verified BLAS-input -> SW-BVH-input handoff can be graph-owned at the callback
    // boundary. Direct and unsplit callers continue through recordPreflightShadowVisibilityResources without
    // deferring this tail.
    [[nodiscard]] bool recordPreflightHybridSoftwareTail(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        bool hardwareBackendReady,
        bool surfelFrameConstantsGraphOwned = false,
        bool shadowMaterialContextBatchGraphOwned = false,
        bool sceneBvhBatchGraphOwned = false,
        bool meshSwBvhBuildsGraphOwned = false,
        bool meshSwBvhInputStatesGraphOwned = false,
        bool hybridHardwareFallbackUploadsGraphOwned = false,
        const void* hybridHardwareFallbackInstanceMaterialData = nullptr,
        usize hybridHardwareFallbackInstanceMaterialByteCount = 0u,
        const void* hybridHardwareFallbackInstanceData = nullptr,
        usize hybridHardwareFallbackInstanceByteCount = 0u,
        const void* hybridHardwareFallbackMaterialTypedData = nullptr,
        usize hybridHardwareFallbackMaterialTypedByteCount = 0u
    );
    [[nodiscard]] bool shadowVisibilityResourcesPreflighted()const noexcept;
    [[nodiscard]] bool shadowVisibilityHardwareSupported()const noexcept;
    [[nodiscard]] bool shadowVisibilitySoftwareResourcesPreflighted()const noexcept;
    [[nodiscard]] bool hybridShadowVisibilityResourcesPreflighted()const noexcept;
    void discardPreflightShadowVisibilityResources()noexcept;
    // These are retained handles for the current frozen trace plan. The graph imports each physical buffer once and
    // uses the shared IDs for every packet that manually stages it.
    [[nodiscard]] bool freezePreparedShadowTraceGeometryBuffers();
    [[nodiscard]] const PreparedShadowTraceGeometryBufferVector& preparedShadowTraceGeometryBuffers()const noexcept;
    [[nodiscard]] const PreparedShadowTraceMaterialSampledTextureVector&
        preparedShadowTraceMaterialSampledTextures()const noexcept;
    // Includes accepted but currently invisible mesh streams, which must retain their state source until their
    // owning mesh is removed or a later preparation packet supersedes it.
    [[nodiscard]] const Vector<Core::BufferHandle, Core::Alloc::GlobalArena>& acceptedShadowTraceGeometryBuffers()const noexcept;
    void confirmPreparedShadowTraceGeometryNormalization()noexcept;
    void invalidatePreparedShadowTraceGeometryBuffers()noexcept;

    [[nodiscard]] bool buildPendingMeshBlas(Core::CommandList& commandList);
    [[nodiscard]] bool buildPendingMeshSwBvh(Core::CommandList& commandList);
    [[nodiscard]] bool buildSceneTlas(
        Core::CommandList& commandList,
        Core::Alloc::ScratchArena& scratchArena,
        bool shadowMaterialContextBatchGraphOwned = false
    );
    [[nodiscard]] bool buildSceneSwBvh(
        Core::CommandList& commandList,
        Core::Alloc::ScratchArena& scratchArena,
        bool shadowMaterialContextBatchGraphOwned = false,
        bool sceneBvhBatchGraphOwned = false,
        bool meshSwBvhBuildsGraphOwned = false
    );
    void releaseCausticEmissionTargetHeapHandle();
    [[nodiscard]] bool createShadowVisibilityTarget(DeferredFrameTargets& targets);
    [[nodiscard]] bool createCausticTargets(DeferredFrameTargets& targets);
    // Resolve the frozen shared material-context heap slots after preflight has settled all backing-buffer capacities.
    // The shared graph retains this POD as an immutable upload blob before recording begins.
    [[nodiscard]] bool snapshotRayTraceMaterialContextSlots(RayTraceMaterialContextSlots& outSlots);
    // Retain the exact preflight-gathered caustic AABB stream as an immutable graph blob. A valid empty result
    // authoritatively represents a frame without refractive emission targets.
    [[nodiscard]] bool retainPreparedCausticEmissionTargetUpload(
        Core::GpuTaskGraph& graph,
        Core::GpuUploadBlobId& outBlob
    )const;
    // Retain the exact per-frame surfel constant payload before graph recording. A valid empty result represents
    // an inactive surfel frame; an active frame always supplies a blob for the graph-owned upload.
    [[nodiscard]] bool retainPreparedSurfelFrameConstantsUpload(
        Core::GpuTaskGraph& graph,
        const DeferredFrameTargets& targets,
        Core::GpuUploadBlobId& outBlob
    )const;
    // The material table, instance stream, and typed bytes share indices and offsets, so retain them only as one
    // preflight-frozen graph upload batch.
    [[nodiscard]] bool retainPreparedShadowMaterialContextUploads(
        Core::GpuTaskGraph& graph,
        Core::GpuUploadBlobId& outInstanceMaterialBlob,
        Core::GpuUploadBlobId& outInstanceBlob,
        Core::GpuUploadBlobId& outMaterialTypedBlob
    )const;
    // A healthy hybrid preflight retains an immutable hardware context before the final software context replaces
    // it. The optional tail may need that exact hardware snapshot again, so retain three graph-owned blobs without
    // allowing a late recorder to re-read the renderer/material stream. An absent snapshot is a valid request for
    // the existing direct compatibility retry.
    [[nodiscard]] bool retainPreparedHybridHardwareMaterialContextFallbackUploads(
        Core::GpuTaskGraph& graph,
        Core::GpuUploadBlobId& outInstanceMaterialBlob,
        Core::GpuUploadBlobId& outInstanceBlob,
        Core::GpuUploadBlobId& outMaterialTypedBlob
    )const;
    // Records the retained hardware fallback against graph-owned immutable bytes. This is intentionally separate
    // from the stale-snapshot direct retry, which remains the narrow compatibility boundary after validation fails.
    [[nodiscard]] bool recordPreparedHybridHardwareMaterialContextFallback(
        Core::CommandList& commandList,
        const void* instanceMaterialData,
        usize instanceMaterialByteCount,
        const void* instanceData,
        usize instanceByteCount,
        const void* materialTypedData,
        usize materialTypedByteCount
    );
    // Compatibility-only overload for callers that do not have a graph-owned upload blob.
    [[nodiscard]] bool recordPreparedHybridHardwareMaterialContextFallback(Core::CommandList& commandList);
    void confirmPreparedShadowMaterialContextUploads()noexcept;
    // The software scene hierarchy and its leaf instances share topology and leaf indices, so retain them as one
    // immutable preflight batch and publish both only when the accepting Shadow Preparation packet submits.
    [[nodiscard]] bool retainPreparedSceneBvhUploads(
        Core::GpuTaskGraph& graph,
        Core::GpuUploadBlobId& outNodeBlob,
        Core::GpuUploadBlobId& outInstanceBlob
    )const;
    void confirmPreparedSceneBvhUploads()noexcept;
#if !defined(NWB_FINAL)
    // One-shot test seam for the healthy hybrid tail. It models a traversal-table miss whose direct revalidation also
    // cannot record, so Shadow Preparation must retain the opaque-HW fallback without accepting stale SW state.
    void forceHybridSceneTraversalFallbackForTesting()noexcept;
    // Target-hardware benchmark seam. It retains the normal opaque-HW fallback on every hybrid frame without
    // flooding the diagnostic log, so a fixed scene can compare that boundary against the healthy hybrid tail.
    void forceHybridSceneTraversalFallbackEveryFrameForTesting()noexcept;
    // Makes the retained HW fallback snapshot fail validation once, proving the direct hardware retry boundary.
    void forceHybridHardwareFallbackSnapshotStaleForTesting()noexcept;
#endif
    // Opaque and healthy hybrid hardware TLAS work records from this frozen preflight plan in Shadow Preparation.
    // Its static cache becomes valid only after that packet accepts; a hybrid record miss retries direct TLAS work.
    [[nodiscard]] bool preparedSceneTlasBuildReady()const noexcept;
    void confirmPreparedSceneTlasBuild()noexcept;
    // Opaque and independent hybrid hardware BLAS build/refit choices retain their selected handles through
    // recording. Hybrid mismatch falls back to the established direct loop; only Shadow Preparation acceptance
    // publishes a frozen plan's mesh-cache progress.
    [[nodiscard]] bool preparedMeshBlasBuildsReady()const noexcept;
    [[nodiscard]] const PreparedMeshBlasBuildVector& preparedMeshBlasBuilds()const noexcept;
    void confirmPreparedMeshBlasBuilds()noexcept;
    // Software-only frames and the independent per-mesh portion of hybrid frames freeze selected build/refit work
    // against its shared scratch generation. Hybrid scene/material snapshots remain independently graph-owned while
    // their optional software tail preserves its narrow direct compatibility fallback.
    [[nodiscard]] bool preparedMeshSwBvhBuildsReady()const noexcept;
    [[nodiscard]] const PreparedMeshSwBvhBuildVector& preparedMeshSwBvhBuilds()const noexcept;
    // The pure-software Shadow Preparation packet records each frozen build after graph-owned typed sentinel
    // clears.  Revalidate this immutable snapshot immediately before its native compute sequence; any miss rejects
    // the shared packet so the existing acceptance callback cannot publish partial topology.
    [[nodiscard]] bool recordPreparedMeshSwBvhBuildAfterGraphClears(
        Core::CommandList& commandList,
        const PreparedMeshSwBvhBuild& build
    );
    void confirmPreparedMeshSwBvhBuilds()noexcept;
    void releaseRayTraceMaterialContextHeapHandles();
    void releaseSwBvhScratchHeapHandles();
    void releaseSurfelGiHeapHandles();
    // The shared deferred graph declares the hardware trace entry resources. Direct compatibility callers retain
    // their native state setup by leaving graphEntryStatesOwned false.
    [[nodiscard]] bool renderShadowVisibility(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        bool graphEntryStatesOwned = false,
        bool splitSoftTransparentFold = false,
        u32* opaqueFrameIndex = nullptr,
        bool graphOwnsOpaqueTemporalMergeEntryStates = false,
        bool splitOpaqueSoftResolve = false
    );
    [[nodiscard]] Core::GpuTaskId declareShadowVisibilityTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        DeferredFrameTargets& targets,
        const bool* prepared,
        bool hardwareShadowSupported,
        Core::GpuTimingSubmissionTicket& timingTicket,
        bool graphEntryStatesOwned = false,
        bool graphOwnsAllLitVisibilityClear = false,
        GraphOwnedAdaptiveShadowPrimitivePlan graphOwnedAdaptivePrimitives = {}
    );
    // A prepared soft-transparent frame splits opaque soft visibility from its transparent fold while retaining one
    // semantic Shadow Visibility packet. The opaque task starts the legacy timing scopes; the terminal fold task
    // closes them and remains the accepted output owner.
    [[nodiscard]] Core::GpuTaskId declareShadowVisibilityOpaqueTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        DeferredFrameTargets& targets,
        const bool* prepared,
        bool hardwareShadowSupported,
        Core::GpuTimingSubmissionTicket& timingTicket,
        Optional<Core::GpuTimingMeasure>* asyncTiming,
        Optional<Core::GpuTimingMeasure>* shadowVisibilityTiming,
        bool* opaqueProduced,
        u32* opaqueFrameIndex,
        bool graphEntryStatesOwned = false,
        bool graphOwnsOpaqueTemporalMergeEntryStates = false
    );
    // The prepared opaque producer records the trace and geometry downsample first. This adjacent callback owns
    // the compiler-lowered trace/geometry sampled handoff before temporal merge and the first wavelet; its native
    // tail retains dynamic ping-pong and upsample work while the terminal fold remains the accepted output owner.
    [[nodiscard]] Core::GpuTaskId declareShadowVisibilityOpaqueFirstWaveletTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        DeferredFrameTargets& targets,
        Core::GpuTimingSubmissionTicket& timingTicket,
        Optional<Core::GpuTimingMeasure>* asyncTiming,
        Optional<Core::GpuTimingMeasure>* shadowVisibilityTiming,
        Optional<Core::GpuTimingMeasure>* opaqueResolveTiming,
        bool* opaqueProduced,
        const u32* opaqueFrameIndex,
        bool hardwareShadowSupported,
        bool graphEntryStatesOwned = false,
        bool graphOwnsOpaqueTemporalMergeEntryStates = false
    );
    [[nodiscard]] Core::GpuTaskId declareShadowVisibilityOpaqueResolveTailTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        DeferredFrameTargets& targets,
        Core::GpuTimingSubmissionTicket& timingTicket,
        Optional<Core::GpuTimingMeasure>* asyncTiming,
        Optional<Core::GpuTimingMeasure>* shadowVisibilityTiming,
        Optional<Core::GpuTimingMeasure>* opaqueResolveTiming,
        bool* opaqueProduced,
        const u32* opaqueFrameIndex,
        bool hardwareShadowSupported,
        bool graphEntryStatesOwned = false
    );
    // The prepared transparent temporal merge receives frozen history/moment entry states and starts the resolve
    // timing interval. Its output reaches the first wavelet through the compiler-owned task handoff.
    [[nodiscard]] Core::GpuTaskId declareShadowTransparentSoftTemporalMergeTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        DeferredFrameTargets& targets,
        Core::GpuTimingSubmissionTicket& timingTicket,
        Optional<Core::GpuTimingMeasure>* transparentResolveTiming,
        const bool* opaqueProduced,
        bool* transparentTraceProduced,
        const u32* opaqueFrameIndex,
        bool graphEntryStatesOwned = false,
        bool graphOwnsTransparentTemporalMergeEntryStates = false
    );
    // The prepared transparent trace or temporal merge's first wavelet inherits its graph-declared input and output
    // states. The terminal fold remains responsible for the final upsample, submission acceptance, and history
    // publication.
    [[nodiscard]] Core::GpuTaskId declareShadowTransparentSoftFirstWaveletTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        DeferredFrameTargets& targets,
        Core::GpuTimingSubmissionTicket& timingTicket,
        Optional<Core::GpuTimingMeasure>* transparentResolveTiming,
        const bool* opaqueProduced,
        bool* transparentTraceProduced,
        const u32* opaqueFrameIndex,
        bool graphEntryStatesOwned = false,
        bool graphOwnsTransparentWaveletInputBoundary = false,
        bool startsTransparentResolveTiming = true
    );
    [[nodiscard]] Core::GpuTaskId declareShadowTransparentSoftFoldTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        DeferredFrameTargets& targets,
        Core::GpuTimingSubmissionTicket& timingTicket,
        Optional<Core::GpuTimingMeasure>* asyncTiming,
        Optional<Core::GpuTimingMeasure>* shadowVisibilityTiming,
        Optional<Core::GpuTimingMeasure>* transparentResolveTiming,
        const bool* opaqueProduced,
        bool* transparentTraceProduced,
        const u32* opaqueFrameIndex,
        bool graphEntryStatesOwned = false
    );
    [[nodiscard]] Core::GpuTaskId declareShadowTransparentSoftTraceTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        DeferredFrameTargets& targets,
        Core::GpuTimingSubmissionTicket& timingTicket,
        const bool* opaqueProduced,
        const u32* opaqueFrameIndex,
        bool* transparentTraceProduced,
        bool graphEntryStatesOwned = false
    );
    void clearShadowVisibility(Core::CommandList& commandList, DeferredFrameTargets& targets);
    // Direct compatibility helper for the per-frame non-temporal accumulator reset. The normal deferred graph owns
    // its typed clear and commits the matching CPU reset only after the containing producer packet accepts.
    void clearNonTemporalCausticAccumulator(Core::CommandList& commandList, DeferredFrameTargets& targets);
    void clearCausticTargets(Core::CommandList& commandList, DeferredFrameTargets& targets);
    void confirmCausticAccumulatorNonTemporalClear();
    // The temporal bootstrap clear is recorded by a graph task, but this mirror changes only when the containing
    // caustic producer packet accepts.
    void confirmCausticAccumulatorBootstrapClear();
    // A warm temporal accumulator decays in its own graph task before the selected photon producer.  The task
    // shares the producer packet, so the compiler owns the UAV handoff between the two dispatches.
    [[nodiscard]] Core::GpuTaskId declareCausticAccumulatorDecayTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        DeferredFrameTargets& targets,
        const bool* shadowVisibilityPrepared,
        f32 decayFactor,
        bool hardwareCaustics,
        Core::GpuTimingSubmissionTicket& timingTicket,
        Optional<Core::GpuTimingMeasure>* causticPhotonTiming,
        bool graphEntryStatesOwned = false
    );
    // Record the decay dispatch itself.  Graph callers leave entry state lowering and the following producer's
    // UAV dependency to the compiler; direct compatibility callers retain the existing native setup.
    [[nodiscard]] bool dispatchCausticAccumulatorDecay(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        f32 decayFactor,
        bool graphEntryStatesOwned = false
    );
    // Hybrid mode folds software transparent transmittance onto hardware opaque visibility. The shared deferred
    // graph can supply the traversal entry states; direct compatibility callers retain their native setup.
    [[nodiscard]] bool renderGpuBvhShadowVisibility(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        bool multiplyOntoOpaque = false,
        bool graphEntryStatesOwned = false,
        bool splitSoftTransparentFold = false,
        u32* opaqueFrameIndex = nullptr,
        bool graphOwnsOpaqueTemporalMergeEntryStates = false,
        bool splitOpaqueSoftResolve = false,
        const GraphOwnedAdaptiveShadowPrimitivePlan* graphOwnedAdaptivePrimitives = nullptr
    );
    [[nodiscard]] bool prepareGpuBvhCausticResources(DeferredFrameTargets& targets);
    [[nodiscard]] Core::GpuTaskId declareSoftwareCausticsTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        DeferredFrameTargets& targets,
        const bool* shadowVisibilityPrepared,
        Core::GpuTimingSubmissionTicket& timingTicket,
        bool graphEntryStatesOwned = false,
        bool graphOwnsAccumulatorBootstrapClear = false,
        bool graphOwnsNonTemporalAccumulatorClear = false,
        bool graphOwnsAccumulatorDecay = false,
        bool graphOwnsResolve = false,
        Optional<Core::GpuTimingMeasure>* causticPhotonTiming = nullptr,
        bool* causticProducerDispatched = nullptr
    );
    // The shared deferred graph supplies descriptor-visible shared-deferred entry states. Direct compatibility
    // callers retain the native setup by leaving this false.
    [[nodiscard]] bool renderGpuBvhCaustics(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        bool graphEntryStatesOwned = false,
        bool graphOwnsAccumulatorBootstrapClear = false,
        bool graphOwnsAccumulatorDecay = false,
        bool graphOwnsResolve = false,
        Optional<Core::GpuTimingMeasure>* causticPhotonTiming = nullptr
    );
    [[nodiscard]] bool hasCausticWork()const noexcept;
    [[nodiscard]] bool prepareHwCausticResources(DeferredFrameTargets& targets);
    [[nodiscard]] Core::GpuTaskId declareHardwareCausticsTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        DeferredFrameTargets& targets,
        const bool* shadowVisibilityPrepared,
        Core::GpuTimingSubmissionTicket& timingTicket,
        bool graphEntryStatesOwned = false,
        bool graphOwnsAccumulatorBootstrapClear = false,
        bool graphOwnsNonTemporalAccumulatorClear = false,
        bool graphOwnsAccumulatorDecay = false,
        bool graphOwnsResolve = false,
        Optional<Core::GpuTimingMeasure>* causticPhotonTiming = nullptr,
        bool* causticProducerDispatched = nullptr
    );
    // The shared deferred graph supplies descriptor-visible hardware-caustic producer inputs. Direct
    // compatibility callers retain native setup by leaving this false.
    [[nodiscard]] bool renderHwCaustics(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        bool graphEntryStatesOwned = false,
        bool graphOwnsAccumulatorBootstrapClear = false,
        bool graphOwnsAccumulatorDecay = false,
        bool graphOwnsResolve = false,
        Optional<Core::GpuTimingMeasure>* causticPhotonTiming = nullptr
    );
    // The normal deferred graph records geometry downsample, resolve prepare, all five wavelet passes, and upsample
    // after the selected photon producer. Their exact resource uses own the immutable and ping-pong UAV-to-SRV
    // handoffs; a final empty callback preserves the established resolve timing endpoint. Direct compatibility
    // callers keep the full resolve attached to it.
    [[nodiscard]] Core::GpuTaskId declareCausticGeometryDownsampleTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        DeferredFrameTargets& targets,
        Core::GpuTimingSubmissionTicket& timingTicket,
        const bool* causticProducerDispatched,
        Optional<Core::GpuTimingMeasure>* causticResolveTiming,
        bool graphEntryStatesOwned = false
    );
    [[nodiscard]] Core::GpuTaskId declareCausticResolveTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        Core::GpuTimingSubmissionTicket& timingTicket,
        const bool* causticProducerDispatched,
        Optional<Core::GpuTimingMeasure>* causticResolveTiming
    );
    [[nodiscard]] Core::GpuTaskId declareCausticResolvePrepareTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        DeferredFrameTargets& targets,
        const bool* causticProducerDispatched,
        bool graphEntryStatesOwned = false
    );
    [[nodiscard]] Core::GpuTaskId declareCausticResolveWaveletTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        DeferredFrameTargets& targets,
        const bool* causticProducerDispatched,
        bool graphEntryStatesOwned = false
    );
    [[nodiscard]] Core::GpuTaskId declareCausticResolveSecondWaveletTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        DeferredFrameTargets& targets,
        const bool* causticProducerDispatched,
        bool graphEntryStatesOwned = false
    );
    [[nodiscard]] Core::GpuTaskId declareCausticResolveThirdWaveletTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        DeferredFrameTargets& targets,
        const bool* causticProducerDispatched,
        bool graphEntryStatesOwned = false
    );
    [[nodiscard]] Core::GpuTaskId declareCausticResolveFourthWaveletTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        DeferredFrameTargets& targets,
        const bool* causticProducerDispatched,
        bool graphEntryStatesOwned = false
    );
    [[nodiscard]] Core::GpuTaskId declareCausticResolveFifthWaveletTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        DeferredFrameTargets& targets,
        const bool* causticProducerDispatched,
        bool graphEntryStatesOwned = false
    );
    [[nodiscard]] Core::GpuTaskId declareCausticResolveUpsampleTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        DeferredFrameTargets& targets,
        const bool* causticProducerDispatched,
        bool graphEntryStatesOwned = false
    );
    // Narrow entries for the graph-owned geometry/prepare/five-wavelet/upsample callbacks. The shared direct
    // implementation remains private and retains its original single-call timing scope.
    void dispatchGraphCausticGeometryDownsample(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        bool graphEntryStatesOwned = false
    );
    void dispatchGraphCausticResolveUpsample(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        bool graphEntryStatesOwned = false
    );
    void dispatchGraphCausticResolvePrepare(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        bool graphEntryStatesOwned = false
    );
    void dispatchGraphCausticResolveWavelet(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        bool graphEntryStatesOwned = false
    );
    void dispatchGraphCausticResolveSecondWavelet(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        bool graphEntryStatesOwned = false
    );
    void dispatchGraphCausticResolveThirdWavelet(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        bool graphEntryStatesOwned = false
    );
    void dispatchGraphCausticResolveFourthWavelet(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        bool graphEntryStatesOwned = false
    );
    void dispatchGraphCausticResolveFifthWavelet(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        bool graphEntryStatesOwned = false
    );
    [[nodiscard]] bool hasHwCausticWork()const noexcept;
    [[nodiscard]] bool hasSurfelWork()const noexcept;
    [[nodiscard]] bool needsSurfelResourceInitialization()const noexcept;
    // Typed graph clear primitives own the persistent-buffer writes. This resource-free task only records and
    // publishes their CPU lifecycle after the shared producer packet has accepted.
    [[nodiscard]] Core::GpuTaskId declareSurfelResourceInitializationLifecycleTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc
    );
    [[nodiscard]] bool recordSurfelResourceInitializationLifecycle()noexcept;
    // Clear ownership commits only after the producer packet accepts; direct compatibility callers use the same
    // lifecycle methods around their retained native clear sequence.
    void finalizeSurfelResourceInitialization();
    void discardSurfelResourceInitialization();
    [[nodiscard]] Core::GpuTaskId declareSurfelGiAgeFreeTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        DeferredFrameTargets& targets,
        Core::GpuTimingSubmissionTicket& timingTicket,
        Optional<Core::GpuTimingMeasure>& asyncTiming,
        bool graphEntryStatesOwned = false
    );
    [[nodiscard]] Core::GpuTaskId declareSurfelGiHashBuildTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        DeferredFrameTargets& targets,
        Core::GpuTimingSubmissionTicket& timingTicket,
        Optional<Core::GpuTimingMeasure>* asyncTiming = nullptr,
        bool graphEntryStatesOwned = false
    );
    [[nodiscard]] Core::GpuTaskId declareSurfelGiSpawnTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        DeferredFrameTargets& targets,
        Core::GpuTimingSubmissionTicket& timingTicket,
        Optional<Core::GpuTimingMeasure>* asyncTiming = nullptr,
        bool graphEntryStatesOwned = false
    );
    [[nodiscard]] Core::GpuTaskId declareSurfelGiTraceBuildArgsTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        DeferredFrameTargets& targets,
        Core::GpuTimingSubmissionTicket& timingTicket,
        Optional<Core::GpuTimingMeasure>* asyncTiming = nullptr,
        bool graphEntryStatesOwned = false
    );
    [[nodiscard]] Core::GpuTaskId declareSurfelGiTraceTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        DeferredFrameTargets& targets,
        Core::GpuTimingSubmissionTicket& timingTicket,
        Optional<Core::GpuTimingMeasure>* asyncTiming = nullptr,
        bool graphEntryStatesOwned = false
    );
    [[nodiscard]] Core::GpuTaskId declareSurfelGiResolveTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        DeferredFrameTargets& targets,
        Core::GpuTimingSubmissionTicket& timingTicket,
        Optional<Core::GpuTimingMeasure>* asyncTiming = nullptr,
        bool graphEntryStatesOwned = false
    );
    [[nodiscard]] Core::GpuTaskId declareSurfelGiTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        DeferredFrameTargets& targets,
        Core::GpuTimingSubmissionTicket& timingTicket,
        bool graphEntryStatesOwned = false,
        bool graphOwnsCellHeadClear = false,
        bool graphOwnsHashBuild = false,
        bool graphOwnsSpawn = false,
        bool graphOwnsTraceBuildArgs = false,
        bool graphOwnsTrace = false,
        bool graphOwnsResolve = false,
        Optional<Core::GpuTimingMeasure>* asyncTiming = nullptr
    );
    // Direct compatibility callers retain the complete native age/free, cell-head-clear, hash-build, Spawn,
    // trace-build-args, trace, resolve, and upsample sequence.
    [[nodiscard]] bool renderSurfelGi(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        bool graphEntryStatesOwned = false
    );
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
    // The graph schedules the small diagnostic copy as a late Transfer-preferred tail and publishes its token.
    [[nodiscard]] bool shouldCaptureSurfelCountReadback()const noexcept;
    void markSurfelCountReadbackScheduled()noexcept;
    [[nodiscard]] bool hybridTransparentShadowReady()const noexcept;
    [[nodiscard]] bool softTransparentShadowReady()const noexcept;
    // Commit soft-shadow history only after ordered submission accepts.
    void finalizeSoftShadowTemporalHistory(DeferredFrameTargets& targets);
    void discardSoftShadowTemporalHistory();
    // Bind shadow readback to its accepted submission token.
    void confirmShadowVisibilitySubmission(const Core::QueueSubmissionToken& submissionToken);
    // A graph-owned primitive chain cannot publish its CPU mirror while recording.  The Shadow Visibility task
    // commits the frozen adaptive tick and optional readback token only after its shared packet accepts.
    void confirmGraphOwnedAdaptiveShadowPrimitiveSubmission(
        const GraphOwnedAdaptiveShadowPrimitivePlan& plan,
        bool adaptiveRouteRecorded,
        const Core::QueueSubmissionToken& submissionToken
    );


private:
    struct SurfelGiInitializationLifecycleGraphTask;
    friend struct RayTracingShadowVisibilityTaskDetail::ShadowVisibilityOpaqueGraphTask;
    friend struct RayTracingShadowVisibilityTaskDetail::ShadowVisibilityOpaqueFirstWaveletGraphTask;
    friend struct RayTracingShadowVisibilityTaskDetail::ShadowVisibilityOpaqueResolveTailGraphTask;
    friend struct RayTracingShadowVisibilityTaskDetail::ShadowTransparentSoftTraceGraphTask;
    friend struct RayTracingShadowVisibilityTaskDetail::ShadowTransparentSoftTemporalMergeGraphTask;
    friend struct RayTracingShadowVisibilityTaskDetail::ShadowTransparentSoftFirstWaveletGraphTask;
    friend struct RayTracingShadowVisibilityTaskDetail::ShadowTransparentSoftFoldGraphTask;
    friend struct RayTracingShadowVisibilityTaskDetail::ShadowVisibilityGraphTask;
    friend struct RayTracingSurfelGiTaskDetail::SurfelGiAgeFreeGraphTask;
    friend struct RayTracingSurfelGiTaskDetail::SurfelGiHashBuildGraphTask;
    friend struct RayTracingSurfelGiTaskDetail::SurfelGiSpawnGraphTask;
    friend struct RayTracingSurfelGiTaskDetail::SurfelGiTraceBuildArgsGraphTask;
    friend struct RayTracingSurfelGiTaskDetail::SurfelGiTraceGraphTask;
    friend struct RayTracingSurfelGiTaskDetail::SurfelGiResolveGraphTask;
    friend struct RayTracingSurfelGiTaskDetail::SurfelGiGraphTask;
    enum class PreparedShadowMaterialContextRoute : u8{
        None,
        Hardware,
        Software,
    };

    [[nodiscard]] bool preparePendingMeshBlasResources();
    [[nodiscard]] bool prepareSceneTlasResources(Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] bool prepareSceneSwBvhResources(Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] bool buildSceneTlasImpl(
        Core::CommandList* commandList,
        Core::Alloc::ScratchArena& scratchArena,
        bool shadowMaterialContextBatchGraphOwned = false
    );
    [[nodiscard]] bool buildSceneSwBvhImpl(
        Core::CommandList* commandList,
        Core::Alloc::ScratchArena& scratchArena,
        bool shadowMaterialContextBatchGraphOwned = false,
        bool sceneBvhBatchGraphOwned = false,
        bool meshSwBvhBuildsGraphOwned = false
    );
    [[nodiscard]] bool prepareCausticEmissionTargetResources(Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] bool recordPreparedCausticEmissionTargets(Core::CommandList& commandList);
    [[nodiscard]] bool capturePreparedShadowMaterialContext(
        PreparedShadowMaterialContextRoute route,
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
    [[nodiscard]] bool matchesPreparedShadowMaterialContext(
        PreparedShadowMaterialContextRoute route,
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
    )const;
    void clearPreparedShadowMaterialContext()noexcept;
    [[nodiscard]] bool appendPreparedShadowTraceMaterialSampledTextures(
        const MaterialSurfaceInfo& materialInfo,
        Core::Alloc::ScratchArena& scratchArena
    );
    void clearPreparedShadowTraceMaterialSampledTextures()noexcept;
    // A healthy hybrid preflight gathers the HW context before the final SW context replaces it. Retain that exact
    // immutable HW payload so an optional SW-tail miss can restore opaque consumers without a recording-time
    // renderer/material regather; stale sources still take the established direct retry.
    [[nodiscard]] bool capturePreparedHybridHardwareMaterialContextFallback();
    void clearPreparedHybridHardwareMaterialContextFallback()noexcept;
    [[nodiscard]] bool capturePreparedSceneBvh(
        bool staticScene,
        u64 staticSceneHash,
        const void* nodeData,
        usize nodeCount,
        usize nodeByteCount,
        const void* instanceData,
        usize instanceCount,
        usize instanceByteCount
    );
    [[nodiscard]] bool matchesPreparedSceneBvh(
        bool staticScene,
        u64 staticSceneHash,
        const void* nodeData,
        usize nodeCount,
        usize nodeByteCount,
        const void* instanceData,
        usize instanceCount,
        usize instanceByteCount
    )const;
    void clearPreparedSceneBvh()noexcept;
    [[nodiscard]] bool capturePreparedSceneSwBvhTraversal(
        const PreparedSceneSwBvhMesh* meshes,
        usize meshCount,
        u32 instanceCount
    );
    // Pure software keeps the descriptor tables populated by preflight and only verifies them while recording;
    // healthy hybrid restores that immutable table after its hardware preparation has repurposed the live state.
    [[nodiscard]] bool recordPreparedSceneSwBvhTraversal(bool restoreMutableTables = true);
    void clearPreparedSceneSwBvhTraversal()noexcept;
    [[nodiscard]] bool capturePreparedSceneTlasBuild(
        bool staticScene,
        u64 staticSceneHash,
        const Vector<Core::RayTracingInstanceDesc, Core::Alloc::ScratchArena>& instances,
        const Vector<Core::RayTracingAccelStructHandle, Core::Alloc::ScratchArena>& instanceBlases
    );
    [[nodiscard]] bool recordPreparedSceneTlasBuild(
        Core::CommandList& commandList,
        bool sceneTlasBuildStatesGraphOwned
    );
    void clearPreparedSceneTlasBuild()noexcept;
    [[nodiscard]] bool capturePreparedMeshBlasBuilds();
    [[nodiscard]] bool recordPreparedMeshBlasBuilds(
        Core::CommandList& commandList,
        bool meshBlasAccelStructStatesGraphOwned,
        bool meshBlasGeometryBuildInputStatesGraphOwned
    );
    void clearPreparedMeshBlasBuilds()noexcept;
    [[nodiscard]] bool capturePreparedMeshSwBvhBuilds();
    [[nodiscard]] bool recordPreparedMeshSwBvhBuilds(
        Core::CommandList& commandList,
        bool meshSwBvhInputStatesGraphOwned
    );
    [[nodiscard]] bool preparedMeshSwBvhBuildMatchesCurrent(const PreparedMeshSwBvhBuild& build);
    [[nodiscard]] bool recordPreparedMeshSwBvhBuild(
        Core::CommandList& commandList,
        const PreparedMeshSwBvhBuild& build,
        bool meshSwBvhInputStatesGraphOwned,
        bool sentinelClearsGraphOwned,
        bool graphBoundaryStatesOwned
    );
    [[nodiscard]] bool preparedMeshSwBvhBuildProducesTopology(const MeshResources& mesh)const noexcept;
    void clearPreparedMeshSwBvhBuilds()noexcept;
    [[nodiscard]] bool prepareSurfelResources(DeferredFrameTargets& targets);
    [[nodiscard]] bool recordPreparedSurfelFrameConstants(Core::CommandList& commandList, DeferredFrameTargets& targets);
    [[nodiscard]] bool initializeSurfelResources(
        Core::CommandList& commandList,
        bool graphEntryStatesOwned = false
    );
    [[nodiscard]] bool renderSurfelGiAgeFree(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        bool graphEntryStatesOwned = false
    );
    [[nodiscard]] bool renderSurfelGiAfterAgeFree(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        bool graphEntryStatesOwned = false,
        bool graphOwnsCellHeadClear = false,
        bool graphOwnsHashBuild = false,
        bool graphOwnsSpawn = false,
        bool graphOwnsTraceBuildArgs = false,
        bool graphOwnsTrace = false,
        bool graphOwnsResolve = false
    );
    [[nodiscard]] bool renderSurfelGiHashBuild(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        bool graphEntryStatesOwned = false
    );
    [[nodiscard]] bool renderSurfelGiSpawn(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        bool graphEntryStatesOwned = false
    );
    [[nodiscard]] bool renderSurfelGiTraceBuildArgs(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        bool graphEntryStatesOwned = false
    );
    [[nodiscard]] bool renderSurfelGiTrace(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        bool graphEntryStatesOwned = false
    );
    [[nodiscard]] bool renderSurfelGiResolve(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        bool graphEntryStatesOwned = false
    );
    [[nodiscard]] bool renderSurfelGiPhases(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        bool graphEntryStatesOwned,
        bool dispatchAgeFree,
        bool dispatchHashBuild,
        bool dispatchSpawn,
        bool dispatchTraceBuildArgs,
        bool dispatchTrace,
        bool dispatchResolve,
        bool dispatchRemaining,
        bool graphOwnsCellHeadClear,
        bool graphOwnsHashBuild,
        bool graphOwnsTraceBuildArgs,
        bool graphOwnsTrace,
        bool graphOwnsResolve
    );
    [[nodiscard]] bool prepareMeshBlasResources(MeshResources& meshResources);
    [[nodiscard]] bool buildMeshBlas(Core::CommandList& commandList, MeshResources& meshResources);
    // Runtime meshes prepare every frame; static meshes remain dirty until first build.
    [[nodiscard]] bool preparePendingMeshSwBvhResources();
    [[nodiscard]] bool ensureShadowPipeline();
    // Hardware soft trace feeds the shared software denoise chain.
    [[nodiscard]] bool ensureShadowSoftPipeline();
    // Hardware shadow layouts are push-only; resources come from heap sets.
    void appendShadowTraceBindingLayout(Core::BindingLayoutDesc& layoutDesc)const;
    [[nodiscard]] bool ensureSwShadowPipeline();
    [[nodiscard]] bool ensureSwShadowPassPipeline(Core::ShaderHandle& shader, Core::ComputePipelineHandle& pipeline, const Name& shaderName, const char* debugLabel);
    [[nodiscard]] bool ensureSoftShadowResolvePipeline();
    [[nodiscard]] bool ensureShadowGeometryDownsamplePipeline();
    [[nodiscard]] bool ensureSoftTransparentResolvePipeline();
    // Resolve resources pair backing targets with their pushed heap slots.
    struct SoftShadowResolvePassResources{
        Core::Texture* softHalfTexture = nullptr;
        Core::Texture* inputColorTexture = nullptr;
        Core::Texture* momentsTexture = nullptr;
        Core::Texture* outputTexture = nullptr;
        u32 softHalf = 0u;
        u32 inputColor = 0u;
        u32 moments = 0u;
        u32 outputStorage = 0u;
    };
    // Heap-only soft resolve dispatch description.
    struct SoftShadowResolveDispatch{
        Core::ComputePipeline* pipeline = nullptr;
        SoftShadowResolvePassResources firstWaveletResources;
        SoftShadowResolvePassResources outputHalfAResources;
        SoftShadowResolvePassResources outputHalfBResources;
        SoftShadowResolvePassResources upsampleResources;
        Core::Texture* visibilityTexture = nullptr;
        u32 visibilityStorage = 0u;
        u32 sceneShading = 0u;
        bool temporalMomentsValid = false;
        // The prepared graph may already lower a transparent trace or temporal-merge output before the first wavelet.
        bool graphOwnsFirstWaveletInputState = false;
        // A split transparent temporal merge also publishes the selected moments input for the first wavelet.
        bool graphOwnsWaveletMomentsEntryState = false;
        // The split opaque first-wavelet callback inherits its output UAV state from the graph.
        bool graphOwnsFirstWaveletOutputState = false;
        // The transparent fold can inherit its geometry read from the graph; opaque geometry still transitions
        // locally after its in-callback downsample.
        bool graphOwnsWaveletGeometryEntryState = false;
        // Both prepared opaque and transparent resolve callbacks inherit these descriptor-visible upsample reads.
        bool graphOwnsUpsampleStaticEntryStates = false;
        // The one-wavelet opaque resolve tail inherits both of these exact states from the preceding callback.
        bool graphOwnsUpsampleInputColorEntryState = false;
        bool graphOwnsUpsampleVisibilityOutputState = false;
        bool firstWaveletWritesHalfA = true;
        SoftShadowUpsampleFold::Enum fold = SoftShadowUpsampleFold::Overwrite;
        // Must be odd so the selected upsample input is the final ping-pong result.
        u32 waveletPassCount = 1u;
    };
    // Resolve a contiguous shadow-slot range in one heap-selected dispatch.
    void dispatchSoftShadowResolve(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        u32 slotStart,
        u32 slotCount,
        const SoftShadowResolveDispatch& dispatch,
        bool dispatchFirstWavelet = true,
        bool dispatchTail = true
    );
    // Denoise either backend's soft trace and optionally run the transparent trace and resolve phases. The shared
    // deferred graph supplies the first geometry-downsample entry states, the opaque geometry-to-resolve handoff,
    // and prepared transparent trace-to-merge/first-wavelet plus temporal-merge-to-wavelet handoffs; later
    // lifecycle transitions remain task-local.
    void dispatchSoftShadowDenoiseAndTransparentFold(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        u32 frameIndex,
        u32 softGroupsX,
        u32 softGroupsY,
        bool graphEntryStatesOwned = false,
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
        bool dispatchTransparentTemporalMerge = false
    );
    // Graph-only phase helpers preserve the complete direct route above while exposing both in-packet handoffs to
    // the shared deferred graph.
    [[nodiscard]] bool renderShadowVisibilityOpaque(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        u32& outFrameIndex,
        bool graphEntryStatesOwned,
        bool graphOwnsOpaqueTemporalMergeEntryStates
    );
    [[nodiscard]] bool renderSoftOpaqueShadowFirstWavelet(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        u32 frameIndex,
        bool hardwareShadowSupported,
        bool graphEntryStatesOwned,
        bool graphOwnsOpaqueTemporalMergeEntryStates
    );
    [[nodiscard]] bool renderSoftOpaqueShadowResolveTail(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        u32 frameIndex,
        bool hardwareShadowSupported,
        bool graphEntryStatesOwned
    );
    [[nodiscard]] bool renderGpuBvhShadowVisibilityOpaque(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        u32& outFrameIndex,
        bool graphEntryStatesOwned,
        bool graphOwnsOpaqueTemporalMergeEntryStates
    );
    [[nodiscard]] bool renderSoftTransparentShadowTrace(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        u32 frameIndex,
        bool graphEntryStatesOwned,
        bool graphOwnsOpaqueToTransparentBoundary
    );
    [[nodiscard]] bool renderSoftTransparentShadowTemporalMerge(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        u32 frameIndex,
        bool graphEntryStatesOwned,
        bool graphOwnsTransparentTemporalMergeEntryStates
    );
    [[nodiscard]] bool renderSoftTransparentShadowFirstWavelet(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        u32 frameIndex,
        bool graphEntryStatesOwned,
        bool graphOwnsTransparentWaveletInputBoundary
    );
    [[nodiscard]] bool renderSoftTransparentShadowFold(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        u32 frameIndex,
        bool graphEntryStatesOwned
    );
    // Temporal merge precedes soft resolve and swaps history at frame end.
    [[nodiscard]] bool ensureShadowReprojectMergePipeline();
    [[nodiscard]] bool softShadowTemporalHistoryUsable()const noexcept;
    void swapSoftShadowTemporalHistory(DeferredFrameTargets& targets);
    [[nodiscard]] bool ensureSwCausticPipeline();
    [[nodiscard]] bool ensureCausticMaterialContextSlotsHeapHandle();
    [[nodiscard]] bool ensureCausticResolvePipeline();
    [[nodiscard]] bool ensureCausticGeometryDownsamplePipeline();
    [[nodiscard]] bool causticResolveResourcesReady(const DeferredFrameTargets& targets, f32 temporalDecay)const;
    [[nodiscard]] bool ensureCausticAccumulatorDecayPipeline();
    [[nodiscard]] f32 causticTemporalDecay();
    // Temporal phase changes only after accepted producer updates.
    [[nodiscard]] u32 causticTemporalPhaseCount();
    void advanceCausticTemporalReuse();
    // Bootstrap or decay temporal splat accumulation before photon atomic adds.
    void prepareCausticAccumulatorForSplat(Core::CommandList& commandList, DeferredFrameTargets& targets, f32 decayFactor);
    // Shared software/hardware caustic resolve. Normal graph callers split geometry, prepare, and all five wavelet
    // stages so they declare immutable inputs and ping-pong handoffs; direct compatibility callers retain setup.
    void dispatchCausticResolve(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        bool graphEntryStatesOwned = false
    );
    void dispatchCausticGeometryDownsample(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        bool graphEntryStatesOwned = false
    );
    void dispatchCausticResolvePrepare(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        bool graphEntryStatesOwned = false,
        bool graphOwnsPassEntryStates = false
    );
    void dispatchCausticResolveFirstWavelet(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        bool graphEntryStatesOwned = false,
        bool graphOwnsPassEntryStates = false
    );
    void dispatchCausticResolveSecondWavelet(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        bool graphEntryStatesOwned = false,
        bool graphOwnsPassEntryStates = false
    );
    void dispatchCausticResolveThirdWavelet(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        bool graphEntryStatesOwned = false,
        bool graphOwnsPassEntryStates = false
    );
    void dispatchCausticResolveFourthWavelet(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        bool graphEntryStatesOwned = false,
        bool graphOwnsPassEntryStates = false
    );
    void dispatchCausticResolveFifthWavelet(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        bool graphEntryStatesOwned = false,
        bool graphOwnsPassEntryStates = false
    );
    void dispatchCausticWaveletResolve(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        bool graphEntryStatesOwned = false,
        bool graphOwnsPassEntryStates = false
    );
    [[nodiscard]] bool ensureCausticRtPipeline();
    [[nodiscard]] bool ensureBvhSortPipeline();
    [[nodiscard]] bool ensureBvhSortBuffers(usize paddedCount);
    [[nodiscard]] bool bvhBitonicSort(Core::CommandList& commandList, u32 elementCount, u32 paddedCount);
    [[nodiscard]] bool ensureBvhBuildPipeline();
    [[nodiscard]] bool ensureBvhVisitCounterBuffer(usize primitiveCount);
    [[nodiscard]] bool createMeshBvhStorage(usize primitiveCount, Core::BufferHandle& nodeBuffer, Core::BufferHandle& parentBuffer, Core::GpuDescriptorHandle& nodeHeapHandle, Core::GpuDescriptorHandle& parentHeapHandle);
    [[nodiscard]] bool ensureMeshSwBvhResources(u32 primitiveCount, Core::BufferHandle& nodeBuffer, Core::BufferHandle& parentBuffer, Core::GpuDescriptorHandle& nodeHeapHandle, Core::GpuDescriptorHandle& parentHeapHandle);
    [[nodiscard]] bool meshSwBvhResourcesReady(const Core::BufferHandle& nodeBuffer, const Core::BufferHandle& parentBuffer, Core::GpuDescriptorHandle nodeHeapHandle, Core::GpuDescriptorHandle parentHeapHandle);
    [[nodiscard]] bool buildMeshSwBvh(Core::CommandList& commandList, u32 positionHeapSlot, u32 triangleIndexHeapSlot, u32 primitiveCount, const SIMDVector aabbMin, const SIMDVector aabbMax, Core::BufferHandle& nodeBuffer, Core::BufferHandle& parentBuffer, Core::GpuDescriptorHandle& nodeHeapHandle, Core::GpuDescriptorHandle& parentHeapHandle);
    [[nodiscard]] bool buildMeshSwBvhPrepared(Core::CommandList& commandList, u32 positionHeapSlot, u32 triangleIndexHeapSlot, u32 primitiveCount, const SIMDVector aabbMin, const SIMDVector aabbMax, Core::BufferHandle& nodeBuffer, Core::BufferHandle& parentBuffer, Core::GpuDescriptorHandle nodeHeapHandle, Core::GpuDescriptorHandle parentHeapHandle, bool sentinelClearsGraphOwned = false, bool graphBoundaryStatesOwned = false);
    [[nodiscard]] bool refitMeshSwBvh(Core::CommandList& commandList, u32 positionHeapSlot, u32 triangleIndexHeapSlot, u32 primitiveCount, Core::BufferHandle& nodeBuffer, Core::BufferHandle& parentBuffer, Core::GpuDescriptorHandle& nodeHeapHandle, Core::GpuDescriptorHandle& parentHeapHandle);
    [[nodiscard]] bool refitMeshSwBvhPrepared(Core::CommandList& commandList, u32 positionHeapSlot, u32 triangleIndexHeapSlot, u32 primitiveCount, Core::BufferHandle& nodeBuffer, Core::BufferHandle& parentBuffer, Core::GpuDescriptorHandle nodeHeapHandle, Core::GpuDescriptorHandle parentHeapHandle, bool sentinelClearsGraphOwned = false, bool graphBoundaryStatesOwned = false);
    [[nodiscard]] bool updateMeshSwBvh(Core::CommandList& commandList, MeshResources& meshResources);
    [[nodiscard]] bool ensureSceneBvhBuffers(u32 instanceCount);
    [[nodiscard]] bool ensureRayTraceMaterialContextSlotsBuffer();
    [[nodiscard]] bool ensureRayTraceMaterialContextSlotsHeapHandle();
    [[nodiscard]] bool ensureRayTraceMaterialContextHeapHandle(Core::Buffer& buffer, Core::GpuDescriptorHandle& handle);
    [[nodiscard]] bool replaceRayTraceMaterialContextHeapHandle(Core::Buffer& buffer, Core::GpuDescriptorHandle& handle);
    // Stage shared software-BVH traversal inputs.
    void transitionSwShadowTraversalResources(Core::CommandList& commandList);
    [[nodiscard]] bool ensureCausticEmissionTargetBuffer(usize targetCount);
    [[nodiscard]] bool ensureShadowInstanceMaterialBuffer(usize instanceCount);
    [[nodiscard]] bool uploadShadowMaterialContextBuffers(
        Core::CommandList& commandList,
        const InstanceGpuDataVector& instanceData,
        const MaterialTypedByteDataVector& materialTypedBytes
    );
    [[nodiscard]] bool ensureShadowInstanceContextBuffer(usize instanceCount);
    [[nodiscard]] bool ensureShadowMaterialTypedBuffer(usize byteCount);

private:
    PreparedShadowTraceGeometryBufferVector m_preparedShadowTraceGeometryBuffers;
    Vector<Core::BufferHandle, Core::Alloc::GlobalArena> m_acceptedShadowTraceGeometryBuffers;
    PreparedShadowTraceMaterialSampledTextureVector m_preparedShadowTraceMaterialSampledTextures;
    // Persist across graph declaration/recording so the immutable blob and compatibility writer never regather
    // mutable renderer state after preflight. The bytes are tightly packed NwbCausticEmissionTargetGpu records.
    Vector<u8, Core::Alloc::GlobalArena> m_preparedCausticEmissionTargetBytes;
    // The complete shadow material context remains retained until the accepting Shadow Preparation packet commits its
    // static cache. Each vector is one ABI-coupled part of the same payload; never graph-upload one independently.
    Vector<u8, Core::Alloc::GlobalArena> m_preparedShadowInstanceMaterialBytes;
    Vector<u8, Core::Alloc::GlobalArena> m_preparedShadowInstanceBytes;
    Vector<u8, Core::Alloc::GlobalArena> m_preparedShadowMaterialTypedBytes;
    Core::BufferHandle m_preparedShadowInstanceMaterialBuffer;
    Core::BufferHandle m_preparedShadowInstanceBuffer;
    Core::BufferHandle m_preparedShadowMaterialTypedBuffer;
    Core::GpuDescriptorHandle m_preparedShadowInstanceMaterialHeapHandle;
    Core::GpuDescriptorHandle m_preparedShadowInstanceHeapHandle;
    Core::GpuDescriptorHandle m_preparedShadowMaterialTypedHeapHandle;
    usize m_preparedShadowInstanceMaterialCount = 0u;
    usize m_preparedShadowInstanceCount = 0u;
    usize m_preparedShadowMaterialTypedUploadBytes = 0u;
    usize m_preparedShadowInstanceMaterialCapacity = 0u;
    usize m_preparedShadowInstanceCapacity = 0u;
    usize m_preparedShadowMaterialTypedCapacity = 0u;
    u64 m_preparedShadowMaterialContextHash = 0u;
    PreparedShadowMaterialContextRoute m_preparedShadowMaterialContextRoute = PreparedShadowMaterialContextRoute::None;
    bool m_preparedShadowMaterialContextStatic = false;
    bool m_preparedShadowMaterialContextReady = false;
    // The transient HW fallback is separate from the final SW graph upload. It retains only the material-context
    // payload because the preceding Shadow Preparation work has already recorded the frozen HW TLAS/BLAS plan.
    Vector<u8, Core::Alloc::GlobalArena> m_preparedHybridHardwareFallbackBytes;
    Core::BufferHandle m_preparedHybridHardwareFallbackInstanceMaterialBuffer;
    Core::BufferHandle m_preparedHybridHardwareFallbackInstanceBuffer;
    Core::BufferHandle m_preparedHybridHardwareFallbackMaterialTypedBuffer;
    Core::GpuDescriptorHandle m_preparedHybridHardwareFallbackInstanceMaterialHeapHandle;
    Core::GpuDescriptorHandle m_preparedHybridHardwareFallbackInstanceHeapHandle;
    Core::GpuDescriptorHandle m_preparedHybridHardwareFallbackMaterialTypedHeapHandle;
    usize m_preparedHybridHardwareFallbackInstanceMaterialByteCount = 0u;
    usize m_preparedHybridHardwareFallbackInstanceByteCount = 0u;
    usize m_preparedHybridHardwareFallbackMaterialTypedByteCount = 0u;
    usize m_preparedHybridHardwareFallbackInstanceMaterialCapacity = 0u;
    usize m_preparedHybridHardwareFallbackInstanceCapacity = 0u;
    usize m_preparedHybridHardwareFallbackMaterialTypedCapacity = 0u;
    u64 m_preparedHybridHardwareFallbackMaterialContextHash = 0u;
    u64 m_preparedHybridHardwareFallbackRendererMutationVersion = 0u;
    u64 m_preparedHybridHardwareFallbackTransformMutationVersion = 0u;
    u64 m_preparedHybridHardwareFallbackMaterialMutationVersion = 0u;
    bool m_preparedHybridHardwareFallbackStatic = false;
    bool m_preparedHybridHardwareFallbackReady = false;
    bool m_preparedHybridHardwareFallbackRecorded = false;
    // The CPU-built software scene BVH must retain node and leaf-instance bytes together: each node's leaf range
    // indexes this exact instance stream. Hybrid frames graph-own this independent pair and their final
    // software-compatible material context, while the hardware TLAS plan retains its own retry boundary.
    Vector<u8, Core::Alloc::GlobalArena> m_preparedSceneBvhNodeBytes;
    Vector<u8, Core::Alloc::GlobalArena> m_preparedSceneBvhInstanceBytes;
    Core::BufferHandle m_preparedSceneBvhNodeBuffer;
    Core::BufferHandle m_preparedSceneBvhInstanceBuffer;
    Core::GpuDescriptorHandle m_preparedSceneBvhNodeHeapHandle;
    Core::GpuDescriptorHandle m_preparedSceneBvhInstanceHeapHandle;
    usize m_preparedSceneBvhNodeCount = 0u;
    usize m_preparedSceneBvhInstanceCount = 0u;
    usize m_preparedSceneBvhNodeCapacity = 0u;
    usize m_preparedSceneBvhInstanceCapacity = 0u;
    u64 m_preparedSceneBvhStaticSceneHash = 0u;
    bool m_preparedSceneBvhStatic = false;
    bool m_preparedSceneBvhReady = false;
    // The graph uploads frozen scene/material bytes, and this companion plan freezes the matching traversal table so
    // healthy hybrid recording need not rebuild CPU scene data. ECS mutation versions preserve the direct retry path.
    PreparedSceneSwBvhMeshVector m_preparedSceneSwBvhMeshes;
    u32 m_preparedSceneSwBvhInstanceCount = 0u;
    u64 m_preparedSceneSwBvhRendererMutationVersion = 0u;
    u64 m_preparedSceneSwBvhTransformMutationVersion = 0u;
    u64 m_preparedSceneSwBvhMaterialMutationVersion = 0u;
    bool m_preparedSceneSwBvhReady = false;
#if !defined(NWB_FINAL)
    bool m_forceHybridSceneTraversalFallbackForTesting = false;
    bool m_forceHybridSceneTraversalFallbackEveryFrameForTesting = false;
    bool m_expectHybridSceneTraversalRecoveryForTesting = false;
    bool m_reportedHybridSceneTraversalFallbackLoopForTesting = false;
    bool m_reportedHybridSceneTraversalFallbackLoopFailureForTesting = false;
    bool m_reportedHybridHardwareFallbackRestoreLoopForTesting = false;
    bool m_forceHybridHardwareFallbackSnapshotStaleForTesting = false;
    bool m_expectHybridHardwareFallbackDirectRetryForTesting = false;
#endif
    // RayTracingInstanceDesc stores raw BLAS pointers, so the frozen TLAS plan retains every corresponding BLAS
    // handle until Shadow Preparation accepts or discards it. The selected TLAS/backing generation is retained too.
    Vector<Core::RayTracingInstanceDesc, Core::Alloc::GlobalArena> m_preparedSceneTlasInstances;
    Vector<Core::RayTracingAccelStructHandle, Core::Alloc::GlobalArena> m_preparedSceneTlasBlases;
    Core::RayTracingAccelStructHandle m_preparedSceneTlas;
    Core::BufferHandle m_preparedSceneTlasBackingBuffer;
    Core::GpuDescriptorHandle m_preparedSceneTlasHeapHandle;
    usize m_preparedSceneTlasMaxInstances = 0u;
    u64 m_preparedSceneTlasStaticSceneHash = 0u;
    bool m_preparedSceneTlasStatic = false;
    bool m_preparedSceneTlasReady = false;
    PreparedMeshBlasBuildVector m_preparedMeshBlasBuilds;
    bool m_preparedMeshBlasBuildsReady = false;
    PreparedMeshSwBvhBuildVector m_preparedMeshSwBvhBuilds;
    bool m_preparedMeshSwBvhBuildsReady = false;
    DeferredFrameTargets* m_shadowVisibilityPreparedTargets = nullptr;
    bool m_shadowVisibilityResourcesPreflighted = false;
    bool m_shadowVisibilityHardwareSupported = false;
    // Recording may only touch allocations selected before the shared graph is compiled. These flags distinguish a
    // usable frozen trace plan from a non-fatal preflight fallback that leaves the effect black for this frame.
    bool m_shadowVisibilityTraceResourcesPreflighted = false;
    bool m_shadowVisibilityHybridResourcesPreflighted = false;
    bool m_shadowVisibilityBackendPipelinePreflighted = false;
    bool m_shadowVisibilityHybridPipelinePreflighted = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

