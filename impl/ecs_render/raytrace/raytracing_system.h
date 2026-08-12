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
        bool meshSwBvhBuildsGraphOwned = false
    );
    [[nodiscard]] bool shadowVisibilityResourcesPreflighted()const noexcept;
    [[nodiscard]] bool shadowVisibilitySoftwareResourcesPreflighted()const noexcept;
    void discardPreflightShadowVisibilityResources()noexcept;
    // These are retained handles for the current frozen trace plan. The graph imports each physical buffer once and
    // uses the shared IDs for every packet that manually stages it.
    [[nodiscard]] bool freezePreparedShadowTraceGeometryBuffers();
    [[nodiscard]] const PreparedShadowTraceGeometryBufferVector& preparedShadowTraceGeometryBuffers()const noexcept;
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
    // Compatibility-only direct writer for callers outside the shared graph path.
    [[nodiscard]] bool uploadRayTraceMaterialContextSlots(Core::CommandList& commandList);
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
    void confirmPreparedShadowMaterialContextUploads()noexcept;
    // The software scene hierarchy and its leaf instances share topology and leaf indices, so retain them as one
    // immutable preflight batch and publish both only when the accepting Shadow Preparation packet submits.
    [[nodiscard]] bool retainPreparedSceneBvhUploads(
        Core::GpuTaskGraph& graph,
        Core::GpuUploadBlobId& outNodeBlob,
        Core::GpuUploadBlobId& outInstanceBlob
    )const;
    void confirmPreparedSceneBvhUploads()noexcept;
#if !defined(NWB_FINAL) || defined(NWB_ENABLE_TEST_FEATURE_OVERRIDES)
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
    void confirmPreparedMeshSwBvhBuilds()noexcept;
    void releaseRayTraceMaterialContextHeapHandles();
    void releaseSwBvhScratchHeapHandles();
    void releaseSurfelGiHeapHandles();
    // The shared deferred graph declares the hardware trace entry resources. Direct compatibility callers retain
    // their native state setup by leaving graphEntryStatesOwned false.
    [[nodiscard]] bool renderShadowVisibility(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        bool graphEntryStatesOwned = false
    );
    [[nodiscard]] Core::GpuTaskId declareShadowVisibilityTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        DeferredFrameTargets& targets,
        const bool* prepared,
        bool hardwareShadowSupported,
        Core::GpuTimingSubmissionTicket& timingTicket,
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
        bool graphEntryStatesOwned = false
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
        Optional<Core::GpuTimingMeasure>* causticPhotonTiming = nullptr,
        bool* accumulatorBootstrapProducerDispatched = nullptr
    );
    // The shared deferred graph supplies descriptor-visible shared-deferred entry states. Direct compatibility
    // callers retain the native setup by leaving this false.
    [[nodiscard]] bool renderGpuBvhCaustics(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        bool graphEntryStatesOwned = false,
        bool graphOwnsAccumulatorBootstrapClear = false,
        bool graphOwnsAccumulatorDecay = false,
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
        Optional<Core::GpuTimingMeasure>* causticPhotonTiming = nullptr,
        bool* accumulatorBootstrapProducerDispatched = nullptr
    );
    // The shared deferred graph supplies descriptor-visible hardware-caustic producer inputs. Direct
    // compatibility callers retain native setup by leaving this false.
    [[nodiscard]] bool renderHwCaustics(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        bool graphEntryStatesOwned = false,
        bool graphOwnsAccumulatorBootstrapClear = false,
        bool graphOwnsAccumulatorDecay = false,
        Optional<Core::GpuTimingMeasure>* causticPhotonTiming = nullptr
    );
    [[nodiscard]] bool hasHwCausticWork()const noexcept;
    [[nodiscard]] bool hasSurfelWork()const noexcept;
    [[nodiscard]] bool needsSurfelResourceInitialization()const noexcept;
    [[nodiscard]] Core::GpuTaskId declareSurfelResourceInitializationTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        bool graphEntryStatesOwned = false
    );
    // Clear ownership commits only after the producer packet accepts.
    void finalizeSurfelResourceInitialization();
    void discardSurfelResourceInitialization();
    [[nodiscard]] Core::GpuTaskId declareSurfelGiTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        DeferredFrameTargets& targets,
        Core::GpuTimingSubmissionTicket& timingTicket,
        bool graphEntryStatesOwned = false
    );
    // The shared deferred graph supplies the descriptor-visible surfel entry states. Direct compatibility callers
    // retain native setup by leaving this false.
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


private:
    struct SurfelGiInitializationGraphTask;
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
    // A healthy hybrid preflight gathers the HW context before the final SW context replaces it. Retain that exact
    // immutable HW payload so an optional SW-tail miss can restore opaque consumers without a recording-time
    // renderer/material regather; stale sources still take the established direct retry.
    [[nodiscard]] bool capturePreparedHybridHardwareMaterialContextFallback();
    [[nodiscard]] bool recordPreparedHybridHardwareMaterialContextFallback(Core::CommandList& commandList);
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
    [[nodiscard]] bool recordPreparedSceneSwBvhTraversal();
    void clearPreparedSceneSwBvhTraversal()noexcept;
    [[nodiscard]] bool capturePreparedSceneTlasBuild(
        bool staticScene,
        u64 staticSceneHash,
        const Vector<Core::RayTracingInstanceDesc, Core::Alloc::ScratchArena>& instances,
        const Vector<Core::RayTracingAccelStructHandle, Core::Alloc::ScratchArena>& instanceBlases
    );
    [[nodiscard]] bool recordPreparedSceneTlasBuild(Core::CommandList& commandList);
    void clearPreparedSceneTlasBuild()noexcept;
    [[nodiscard]] bool capturePreparedMeshBlasBuilds();
    [[nodiscard]] bool recordPreparedMeshBlasBuilds(Core::CommandList& commandList);
    void clearPreparedMeshBlasBuilds()noexcept;
    [[nodiscard]] bool capturePreparedMeshSwBvhBuilds();
    [[nodiscard]] bool recordPreparedMeshSwBvhBuilds(Core::CommandList& commandList);
    [[nodiscard]] bool preparedMeshSwBvhBuildProducesTopology(const MeshResources& mesh)const noexcept;
    void clearPreparedMeshSwBvhBuilds()noexcept;
    [[nodiscard]] bool prepareSurfelResources(DeferredFrameTargets& targets);
    [[nodiscard]] bool recordPreparedSurfelFrameConstants(Core::CommandList& commandList, DeferredFrameTargets& targets);
    [[nodiscard]] bool initializeSurfelResources(
        Core::CommandList& commandList,
        bool graphEntryStatesOwned = false
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
        bool firstWaveletWritesHalfA = true;
        SoftShadowUpsampleFold::Enum fold = SoftShadowUpsampleFold::Overwrite;
        // Must be odd so the selected upsample input is the final ping-pong result.
        u32 waveletPassCount = 1u;
    };
    // Resolve a contiguous shadow-slot range in one heap-selected dispatch.
    void dispatchSoftShadowResolve(Core::CommandList& commandList, DeferredFrameTargets& targets, u32 slotStart, u32 slotCount, const SoftShadowResolveDispatch& dispatch);
    // Denoise either backend's soft trace and optionally fold transparent transmittance. The shared deferred graph
    // supplies the first geometry-downsample entry states; later trace/resolve transitions remain task-local.
    void dispatchSoftShadowDenoiseAndTransparentFold(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        u32 frameIndex,
        u32 softGroupsX,
        u32 softGroupsY,
        bool graphEntryStatesOwned = false
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
    // Shared software/hardware caustic wavelet resolve. Normal graph callers already declare the geometry-downsample
    // entry states; direct compatibility callers retain that native setup.
    void dispatchCausticResolve(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        bool graphEntryStatesOwned = false
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
    [[nodiscard]] bool buildMeshSwBvhPrepared(Core::CommandList& commandList, u32 positionHeapSlot, u32 triangleIndexHeapSlot, u32 primitiveCount, const SIMDVector aabbMin, const SIMDVector aabbMax, Core::BufferHandle& nodeBuffer, Core::BufferHandle& parentBuffer, Core::GpuDescriptorHandle nodeHeapHandle, Core::GpuDescriptorHandle parentHeapHandle);
    [[nodiscard]] bool refitMeshSwBvh(Core::CommandList& commandList, u32 positionHeapSlot, u32 triangleIndexHeapSlot, u32 primitiveCount, Core::BufferHandle& nodeBuffer, Core::BufferHandle& parentBuffer, Core::GpuDescriptorHandle& nodeHeapHandle, Core::GpuDescriptorHandle& parentHeapHandle);
    [[nodiscard]] bool refitMeshSwBvhPrepared(Core::CommandList& commandList, u32 positionHeapSlot, u32 triangleIndexHeapSlot, u32 primitiveCount, Core::BufferHandle& nodeBuffer, Core::BufferHandle& parentBuffer, Core::GpuDescriptorHandle nodeHeapHandle, Core::GpuDescriptorHandle parentHeapHandle);
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
#if !defined(NWB_FINAL) || defined(NWB_ENABLE_TEST_FEATURE_OVERRIDES)
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

