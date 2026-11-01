// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/material/sampled_texture_collection.h>
#include <impl/ecs_render/optics/coincident_volumes.h>
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
using ::NWB::Impl::ComputeSceneSwBvhStaticSceneHash;

// Cross-frame cache pins raw keys; failed registrations retry next gather.
[[nodiscard]] Expected<Core::GpuDescriptorHandle> AcquireMeshHeapHandle(const Core::BufferHandle& bufferHandle, Core::GpuDescriptorHeap& heap, RtMeshHeapHandleCache& cache);

// Evict unseen cache entries; heap quarantine protects in-flight work.
void SweepUnseenMeshHeapHandles(Core::GpuDescriptorHeap& heap, RtMeshHeapHandleCache& cache);
void BeginMeshHeapHandleGather(RtMeshHeapHandleCache& cache)noexcept;

template<typename RayTracingState>
[[nodiscard]] inline b8 HasPreparedShadowMaterialContextBuffers(const RayTracingState& state, const usize materialCount, const usize instanceCount, const usize materialTypedUploadBytes)noexcept(noexcept(DeclVal<void(*)(b8)noexcept>()(
    state.m_shadowInstanceMaterialBuffer
        && state.m_shadowInstanceMaterialCapacity >= materialCount
        && RayTracingDetail::IsHeapHandle(state.m_shadowInstanceMaterialHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        && state.m_shadowInstanceBuffer
        && state.m_shadowInstanceCapacity >= instanceCount
        && RayTracingDetail::IsHeapHandle(state.m_shadowInstanceHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        && state.m_shadowMaterialTypedBuffer
        && state.m_shadowMaterialTypedCapacity >= DeclVal<const usize&>()
        && RayTracingDetail::IsHeapHandle(state.m_shadowMaterialTypedHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
))){
    const usize requiredMaterialTypedBytes = AlignUp(Max<usize>(materialTypedUploadBytes, sizeof(u32)), sizeof(u32));
    return
        state.m_shadowInstanceMaterialBuffer
        && state.m_shadowInstanceMaterialCapacity >= materialCount
        && RayTracingDetail::IsHeapHandle(state.m_shadowInstanceMaterialHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        && state.m_shadowInstanceBuffer
        && state.m_shadowInstanceCapacity >= instanceCount
        && RayTracingDetail::IsHeapHandle(state.m_shadowInstanceHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        && state.m_shadowMaterialTypedBuffer
        && state.m_shadowMaterialTypedCapacity >= requiredMaterialTypedBytes
        && RayTracingDetail::IsHeapHandle(state.m_shadowMaterialTypedHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
    ;
}

template<typename RayTracingState>
[[nodiscard]] inline b8 HasPreparedSceneBvhBuffers(const RayTracingState& state, const u32 instanceCount)noexcept(noexcept(DeclVal<void(*)(b8)noexcept>()(
    state.m_sceneBvhNodeBuffer
        && state.m_sceneInstanceBuffer
        && state.m_sceneBvhNodeCapacity >= DeclVal<const usize&>()
        && state.m_sceneInstanceCapacity >= instanceCount
        && RayTracingDetail::IsHeapHandle(state.m_sceneBvhNodeHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        && RayTracingDetail::IsHeapHandle(state.m_sceneInstanceHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
))){
    NWB_ASSERT(instanceCount > 0u);
    const usize requiredNodeCount = static_cast<usize>(instanceCount) * 2u - 1u;
    return
        state.m_sceneBvhNodeBuffer
        && state.m_sceneInstanceBuffer
        && state.m_sceneBvhNodeCapacity >= requiredNodeCount
        && state.m_sceneInstanceCapacity >= instanceCount
        && RayTracingDetail::IsHeapHandle(state.m_sceneBvhNodeHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        && RayTracingDetail::IsHeapHandle(state.m_sceneInstanceHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
    ;
}

[[nodiscard]] Expected<PreparedMeshBlasBuild> ResolvePreparedMeshBlasBuild(const ECSRenderDetail::MeshRayTracingResourceSnapshot& meshResources)noexcept;
[[nodiscard]] b8 MatchesPreparedMeshBlasBuild(const ECSRenderDetail::MeshRayTracingResourceSnapshot& meshResources, const PreparedMeshBlasBuild& build)noexcept;
[[nodiscard]] b8 RecordPreparedMeshBlasBuild(const PreparedMeshBlasBuild& build, b8 meshBlasGeometryBuildInputStatesGraphOwned, Core::CommandList& commandList);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

