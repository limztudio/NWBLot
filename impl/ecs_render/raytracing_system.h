// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "subsystem_base.h"

#include <core/alloc/scratch.h>
#include <global/simdmath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererRayTracingSystem final : public RendererSystemSubsystemBase<RendererSystem>{
public:
    explicit RendererRayTracingSystem(RendererSystem& renderer);


public:
    void logCapabilityOnce();
    [[nodiscard]] bool buildPendingMeshBlas(Core::CommandList& commandList);
    [[nodiscard]] bool buildPendingMeshSwBvh(Core::CommandList& commandList);
    [[nodiscard]] bool buildSceneTlas(Core::CommandList& commandList, Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] bool buildSceneSwBvh(Core::CommandList& commandList, Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] bool prepareCausticEmissionTargets(Core::CommandList& commandList, Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] bool createShadowVisibilityTarget(DeferredFrameTargets& targets);
    [[nodiscard]] bool createCausticTargets(DeferredFrameTargets& targets);
    [[nodiscard]] bool prepareShadowVisibilityResources(Core::CommandList& commandList, DeferredFrameTargets& targets, Core::Alloc::ScratchArena& scratchArena, bool& outBackendReady);
    [[nodiscard]] bool renderShadowVisibility(Core::CommandList& commandList, DeferredFrameTargets& targets);
    void clearShadowVisibility(Core::CommandList& commandList, DeferredFrameTargets& targets);
    void clearCausticTargets(Core::CommandList& commandList, DeferredFrameTargets& targets);
    [[nodiscard]] bool renderGpuBvhShadowVisibility(Core::CommandList& commandList, DeferredFrameTargets& targets);
    [[nodiscard]] bool prepareGpuBvhCausticResources(DeferredFrameTargets& targets);
    [[nodiscard]] bool renderGpuBvhCaustics(Core::CommandList& commandList, DeferredFrameTargets& targets);
    [[nodiscard]] bool hasCausticWork()const noexcept;
    // Hardware ray-traced caustic photon producer (P4) -- the byte-parallel sibling of the SW producer above, run on
    // the HW branch (RayTracingAccelStruct supported). Reuses the TLAS + the shadow material/geometry buffers + the
    // shared R32_UINT accumulator + resolve; adds only a per-mesh position SRV array for the geometric face normal.
    [[nodiscard]] bool prepareHwCausticResources(DeferredFrameTargets& targets);
    [[nodiscard]] bool renderHwCaustics(Core::CommandList& commandList, DeferredFrameTargets& targets);
    [[nodiscard]] bool hasHwCausticWork()const noexcept;


private:
    [[nodiscard]] bool buildMeshBlas(Core::CommandList& commandList, MeshResources& meshResources);
    [[nodiscard]] bool ensureShadowPipeline();
    [[nodiscard]] bool ensureShadowBindingSet(DeferredFrameTargets& targets);
    [[nodiscard]] bool ensureSwShadowPipeline();
    [[nodiscard]] bool ensureSwShadowBindingSet(DeferredFrameTargets& targets);
    [[nodiscard]] bool ensureSwCausticPipeline();
    [[nodiscard]] bool ensureSwCausticBindingSet(DeferredFrameTargets& targets);
    [[nodiscard]] bool ensureCausticResolvePipeline();
    [[nodiscard]] bool ensureCausticResolveBindingSet(DeferredFrameTargets& targets);
    // Runs the N-pass edge-avoiding a-trous wavelet resolve (shared by the SW + HW caustic paths): converts the splat
    // accumulator to denoised irradiance, ping-ponging the irradiance + scratch buffers so the final pass lands in
    // irradiance. The accumulator must already hold this frame's splat (producer dispatched). Assumes the resolve
    // pipeline + both ping-pong binding sets are ready (ensureCausticResolvePipeline/BindingSet).
    void dispatchCausticResolve(Core::CommandList& commandList, DeferredFrameTargets& targets);
    [[nodiscard]] bool ensureCausticRtPipeline();
    [[nodiscard]] bool ensureCausticRtBindingSet(DeferredFrameTargets& targets);
    [[nodiscard]] bool ensureBvhSortPipeline();
    [[nodiscard]] bool ensureBvhSortBuffers(usize paddedCount);
    [[nodiscard]] bool bvhBitonicSort(Core::CommandList& commandList, u32 elementCount, u32 paddedCount);
    [[nodiscard]] bool ensureBvhBuildPipeline();
    [[nodiscard]] bool ensureBvhVisitCounterBuffer(usize primitiveCount);
    [[nodiscard]] bool createMeshBvhStorage(usize primitiveCount, Core::BufferHandle& nodeBuffer, Core::BufferHandle& parentBuffer);
    [[nodiscard]] bool ensureMeshBvhBindingSet(Core::Buffer* positionBuffer, Core::Buffer* triangleIndexBuffer, Core::Buffer* nodeBuffer, Core::Buffer* parentBuffer, Core::BindingSetHandle& bindingSet);
    [[nodiscard]] bool ensureMeshSwBvhResources(Core::Buffer* positionBuffer, Core::Buffer* triangleIndexBuffer, u32 primitiveCount, Core::BufferHandle& nodeBuffer, Core::BufferHandle& parentBuffer, Core::BindingSetHandle& bindingSet);
    [[nodiscard]] bool buildMeshSwBvh(Core::CommandList& commandList, Core::Buffer* positionBuffer, Core::Buffer* triangleIndexBuffer, u32 primitiveCount, const SIMDVector aabbMin, const SIMDVector aabbMax, Core::BufferHandle& nodeBuffer, Core::BufferHandle& parentBuffer, Core::BindingSetHandle& bindingSet);
    [[nodiscard]] bool refitMeshSwBvh(Core::CommandList& commandList, Core::Buffer* positionBuffer, Core::Buffer* triangleIndexBuffer, u32 primitiveCount, Core::BufferHandle& nodeBuffer, Core::BufferHandle& parentBuffer, Core::BindingSetHandle& bindingSet);
    [[nodiscard]] bool updateMeshSwBvh(Core::CommandList& commandList, MeshResources& meshResources);
    [[nodiscard]] bool ensureSceneBvhBuffers(u32 instanceCount);
    [[nodiscard]] bool ensureCausticEmissionTargetBuffer(usize targetCount);
    [[nodiscard]] bool ensureShadowInstanceMaterialBuffer(usize instanceCount);
    [[nodiscard]] bool uploadShadowMaterialContextBuffers(
        Core::CommandList& commandList,
        const InstanceGpuDataVector& instanceData,
        const MaterialTypedByteDataVector& materialTypedBytes
    );
    [[nodiscard]] bool ensureShadowInstanceContextBuffer(usize instanceCount);
    [[nodiscard]] bool ensureShadowMaterialTypedBuffer(usize byteCount);

#if defined(NWB_DEBUG)
private:
    void runBvhSortSelfTest();
    void runBvhBuildSelfTest();
    void runSceneBvhSelfTest();
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

