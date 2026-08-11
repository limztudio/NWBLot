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
        bool meshBlasBuildsGraphOwned = false
    );
    [[nodiscard]] bool shadowVisibilityResourcesPreflighted()const noexcept;
    void discardPreflightShadowVisibilityResources()noexcept;
    // These are retained handles for the current frozen trace plan. The graph imports each physical buffer once and
    // uses the shared IDs for every packet that manually stages it.
    [[nodiscard]] bool freezePreparedShadowTraceGeometryBuffers();
    [[nodiscard]] const PreparedShadowTraceGeometryBufferVector& preparedShadowTraceGeometryBuffers()const noexcept;
    void normalizePreparedShadowTraceGeometryBuffers(Core::CommandList& commandList)const;
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
        bool sceneBvhBatchGraphOwned = false
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
    // The opaque hardware TLAS records from this frozen preflight plan in Shadow Preparation. Its static cache
    // becomes valid only after that packet accepts; transparent/hybrid frames retain the compatibility path.
    [[nodiscard]] bool preparedSceneTlasBuildReady()const noexcept;
    void confirmPreparedSceneTlasBuild()noexcept;
    // Opaque hardware frames retain exact BLAS build/refit choices. The selected handles outlive recording; only
    // Shadow Preparation acceptance publishes mesh cache progress.
    [[nodiscard]] bool preparedMeshBlasBuildsReady()const noexcept;
    [[nodiscard]] const PreparedMeshBlasBuildVector& preparedMeshBlasBuilds()const noexcept;
    void confirmPreparedMeshBlasBuilds()noexcept;
    void releaseRayTraceMaterialContextHeapHandles();
    void releaseSwBvhScratchHeapHandles();
    void releaseSurfelGiHeapHandles();
    // Normalize inputs shared by independently recorded effect packets.
    void normalizePostGbufferPacketResources(Core::CommandList& commandList, DeferredFrameTargets& targets);
    [[nodiscard]] bool renderShadowVisibility(Core::CommandList& commandList, DeferredFrameTargets& targets);
    [[nodiscard]] Core::GpuTaskId declareShadowVisibilityTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        DeferredFrameTargets& targets,
        const bool* prepared,
        bool hardwareShadowSupported,
        Core::GpuTimingSubmissionTicket& timingTicket
    );
    void clearShadowVisibility(Core::CommandList& commandList, DeferredFrameTargets& targets);
    void clearCausticTargets(Core::CommandList& commandList, DeferredFrameTargets& targets);
    void clearSurfelIrradiance(Core::CommandList& commandList, DeferredFrameTargets& targets);
    // Hybrid mode folds software transparent transmittance onto hardware opaque visibility.
    [[nodiscard]] bool renderGpuBvhShadowVisibility(Core::CommandList& commandList, DeferredFrameTargets& targets, bool multiplyOntoOpaque = false);
    [[nodiscard]] bool prepareGpuBvhCausticResources(DeferredFrameTargets& targets);
    [[nodiscard]] Core::GpuTaskId declareSoftwareCausticsTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        DeferredFrameTargets& targets,
        const bool* shadowVisibilityPrepared,
        Core::GpuTimingSubmissionTicket& timingTicket
    );
    [[nodiscard]] bool renderGpuBvhCaustics(Core::CommandList& commandList, DeferredFrameTargets& targets);
    [[nodiscard]] bool hasCausticWork()const noexcept;
    [[nodiscard]] bool prepareHwCausticResources(DeferredFrameTargets& targets);
    [[nodiscard]] Core::GpuTaskId declareHardwareCausticsTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        DeferredFrameTargets& targets,
        const bool* shadowVisibilityPrepared,
        Core::GpuTimingSubmissionTicket& timingTicket
    );
    [[nodiscard]] bool renderHwCaustics(Core::CommandList& commandList, DeferredFrameTargets& targets);
    [[nodiscard]] bool hasHwCausticWork()const noexcept;
    [[nodiscard]] bool hasSurfelWork()const noexcept;
    [[nodiscard]] bool needsSurfelResourceInitialization()const noexcept;
    [[nodiscard]] Core::GpuTaskId declareSurfelResourceInitializationTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc
    );
    // Clear ownership commits only after the producer packet accepts.
    void finalizeSurfelResourceInitialization();
    void discardSurfelResourceInitialization();
    [[nodiscard]] Core::GpuTaskId declareSurfelGiTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        DeferredFrameTargets& targets,
        Core::GpuTimingSubmissionTicket& timingTicket
    );
    [[nodiscard]] bool renderSurfelGi(Core::CommandList& commandList, DeferredFrameTargets& targets);
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
        bool sceneBvhBatchGraphOwned = false
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
    [[nodiscard]] bool prepareSurfelResources(DeferredFrameTargets& targets);
    [[nodiscard]] bool recordPreparedSurfelFrameConstants(Core::CommandList& commandList, DeferredFrameTargets& targets);
    [[nodiscard]] bool initializeSurfelResources(Core::CommandList& commandList);
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
    // Denoise either backend's soft trace and optionally fold transparent transmittance.
    void dispatchSoftShadowDenoiseAndTransparentFold(Core::CommandList& commandList, DeferredFrameTargets& targets, u32 frameIndex, u32 softGroupsX, u32 softGroupsY);
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
    // Shared software/hardware caustic wavelet resolve.
    void dispatchCausticResolve(Core::CommandList& commandList, DeferredFrameTargets& targets);
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
    // The CPU-built software scene BVH must retain node and leaf-instance bytes together: each node's leaf range
    // indexes this exact instance stream. Hybrid HW-to-SW frames intentionally leave this compatibility-direct.
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

