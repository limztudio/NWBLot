// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/optics/coincident_volumes.h>
#include <impl/ecs_render/raytrace/prepared_scene_bvh_upload.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <impl/ecs_render/shadow/prepared_shadow_material_context.h>

#include <core/graphics/rhi/gpu_descriptor_heap.h>

#include <global/algorithm.h>
#include <global/hash_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RayTracingSoftwareBvhDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using MeshBufferSlotLookup = HashMap<NotNull<Core::Buffer*>, u32, Core::Alloc::ScratchArena, Hasher<NotNull<Core::Buffer*>>, EqualTo<NotNull<Core::Buffer*>>>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void AppendTlasInstanceStaticCacheInput(const Core::RayTracingInstanceDesc& instance, u64& inOutHash)noexcept;
[[nodiscard]] u64 ComputeTlasStaticSceneHash(Span<const Core::RayTracingInstanceDesc> instances)noexcept;
[[nodiscard]] u64 ComputeSceneSwBvhStaticSceneHash(Span<const SceneSwBvhInstanceGpu> instances, Span<const SceneBvhPrimitiveCalculation> primitives)noexcept;

// Cross-frame cache pins raw keys; failed registrations retry next gather.
[[nodiscard]] Expected<Core::GpuDescriptorHandle> AcquireMeshHeapHandle(const Core::BufferHandle& bufferHandle, Core::GpuDescriptorHeap& heap, RtMeshHeapHandleCache& cache);

// Evict unseen cache entries; heap quarantine protects in-flight work.
void SweepUnseenMeshHeapHandles(Core::GpuDescriptorHeap& heap, RtMeshHeapHandleCache& cache);
void BeginMeshHeapHandleGather(RtMeshHeapHandleCache& cache)noexcept;

[[nodiscard]] inline b8 HasPreparedShadowMaterialContextBuffers(const RendererRayTracingState& state, const usize materialCount, const usize instanceCount, const usize materialTypedUploadBytes)noexcept{
    const usize requiredMaterialTypedBytes = AlignUp(Max<usize>(materialTypedUploadBytes, sizeof(u32)), sizeof(u32));
    return PreparedShadowMaterialContext::StorageReady(state, materialCount, instanceCount, requiredMaterialTypedBytes);
}

[[nodiscard]] inline b8 HasPreparedSceneBvhBuffers(const RendererRayTracingState& state, const u32 instanceCount)noexcept{
    NWB_ASSERT(instanceCount > 0u);
    const usize requiredNodeCount = static_cast<usize>(instanceCount) * 2u - 1u;
    return PreparedSceneBvhUpload::StorageReady(state, requiredNodeCount, instanceCount);
}

template<typename ArenaT>
inline void AppendLightSpaceSceneBuffers(
    const ECSRenderDetail::MeshRayTracingResourceSnapshot& meshResources,
    const b8 includeCsgBuffers,
    Vector<Core::BufferHandle, ArenaT>& outBuffers
){
    outBuffers.push_back(meshResources.positionBuffer);
    outBuffers.push_back(meshResources.triangleIndexBuffer);
    outBuffers.push_back(meshResources.attributeBuffer);
    if(meshResources.meshletCount != 0u){
        outBuffers.push_back(meshResources.meshletDescBuffer);
        outBuffers.push_back(meshResources.meshletLocalBoundsBuffer);
    }
    if(includeCsgBuffers){
        if(meshResources.runtimeLocalBoundsBuffer)
            outBuffers.push_back(meshResources.runtimeLocalBoundsBuffer);
        if(meshResources.swBvhNodeBuffer)
            outBuffers.push_back(meshResources.swBvhNodeBuffer);
    }
}

[[nodiscard]] Expected<PreparedMeshBlasBuild> ResolvePreparedMeshBlasBuild(const ECSRenderDetail::MeshRayTracingResourceSnapshot& meshResources)noexcept;
[[nodiscard]] b8 MatchesPreparedMeshBlasBuild(const ECSRenderDetail::MeshRayTracingResourceSnapshot& meshResources, const PreparedMeshBlasBuild& build)noexcept;
[[nodiscard]] b8 RecordPreparedMeshBlasBuild(const PreparedMeshBlasBuild& build, b8 meshBlasGeometryBuildInputStatesGraphOwned, Core::CommandList& commandList);
[[nodiscard]] b8 ResolveShadowOccluderMaterial(
    Core::ECS::EntityID entity,
    const Core::Assets::AssetRef<Material>& material,
    const Scene::TransformComponent* transform,
    u32 meshInstanceIndex,
    RendererMaterialSystem& materialSystem,
    ShadowMaterialSampledTextureCollector& sampledTextureCollector,
    MaterialTypedByteDataVector& shadowMaterialTypedBytes,
    ECSRenderDetail::MaterialTypedConstantRangeMap& shadowConstantTypedRanges,
    ECSRenderDetail::MaterialTypedByteContentRangeMap& shadowMutableTypedRanges,
    RayTracingOpticalSceneGather& opticalScene,
    b8& inOutContentComplete,
    NwbRtInstanceMaterialGpu& outInstanceMaterial,
    InstanceGpuData& outShadowInstance,
    b8* outSceneHasTransparentOccluder = nullptr
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

