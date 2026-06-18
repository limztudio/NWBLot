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
    [[nodiscard]] bool ensureBvhBuildBuffers(usize primitiveCount);
    [[nodiscard]] bool ensureBvhBuildBindingSet(Core::Buffer* positionBuffer, Core::Buffer* triangleIndexBuffer);
    [[nodiscard]] bool buildMeshSwBvh(Core::CommandList& commandList, Core::Buffer* positionBuffer, Core::Buffer* triangleIndexBuffer, u32 primitiveCount, const SIMDVector aabbMin, const SIMDVector aabbMax);

#if defined(NWB_DEBUG)
private:
    void runBvhSortSelfTest();
    void runBvhBuildSelfTest();
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

