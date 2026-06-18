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
    [[nodiscard]] bool createShadowVisibilityTarget(DeferredFrameTargets& targets);
    [[nodiscard]] bool renderShadowVisibility(Core::CommandList& commandList, DeferredFrameTargets& targets);
    void clearShadowVisibility(Core::CommandList& commandList, DeferredFrameTargets& targets);


private:
    [[nodiscard]] bool buildMeshBlas(Core::CommandList& commandList, MeshResources& meshResources);
    [[nodiscard]] bool ensureShadowPipeline();
    [[nodiscard]] bool ensureShadowBindingSet(DeferredFrameTargets& targets);
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

#if defined(NWB_DEBUG)
private:
    void runBvhSortSelfTest();
    void runBvhBuildSelfTest();
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

