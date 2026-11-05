// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/kernel/descriptor_register_utils.h>
#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>

#include <global/simd/vector.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RayTracingDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<RenderableRayTracingMeshResources, RenderableMeshResolution::Enum> ResolveRenderableMeshResources(MeshSystem& meshSystem, RendererMeshSystem& rendererMeshSystem, const Core::ECS::EntityID entity){
    auto resolvedMesh = meshSystem.resolveRenderableMeshStatus(entity);
    if(!resolvedMesh)
        return MakeUnexpected(resolvedMesh.error());
    auto mesh = rendererMeshSystem.findRenderableRayTracingResourceSnapshot(*resolvedMesh);
    if(!mesh)
        return MakeUnexpected(RenderableMeshResolution::Unavailable);
    return RenderableRayTracingMeshResources{ Move(*resolvedMesh), Move(*mesh) };
}

[[nodiscard]] b8 IsHeapHandle(const Core::GpuDescriptorHandle handle, const Core::GpuDescriptorClass::Enum descriptorClass)noexcept{
    return RendererDescriptorRegisterDetail::IsDescriptorClass(handle, descriptorClass);
}

[[nodiscard]] Expected<Core::GpuDescriptorHandle> RegisterHeapBuffer(Core::GpuDescriptorHeap& heap, Core::Buffer& buffer, const Core::GpuDescriptorClass::Enum descriptorClass, const b8 writable){
    const Core::DescriptorWriteItem item = descriptorClass == Core::GpuDescriptorClass::UniformBuffer
        ? Core::DescriptorWriteItem::ConstantBuffer(0u, &buffer)
        : (writable ? Core::DescriptorWriteItem::StructuredBufferUav(0u, &buffer) : Core::DescriptorWriteItem::StructuredBufferSrv(0u, &buffer))
    ;
    return RendererDescriptorRegisterDetail::RegisterDescriptor(heap, descriptorClass, item);
}

[[nodiscard]] b8 EnsureHeapBuffer(const Core::GpuDescriptorClass::Enum descriptorClass, const b8 writable, Core::GpuDescriptorHeap& heap, Core::Buffer& buffer, Core::GpuDescriptorHandle& inOutHandle){
    if(inOutHandle.valid())
        return IsHeapHandle(inOutHandle, descriptorClass);

    const auto acquired = RegisterHeapBuffer(heap, buffer, descriptorClass, writable);
    if(!acquired)
        return false;
    inOutHandle = *acquired;
    return true;
}

[[nodiscard]] b8 ReplaceHeapBuffer(const Core::GpuDescriptorClass::Enum descriptorClass, const b8 writable, Core::GpuDescriptorHeap& heap, Core::Buffer& buffer, Core::GpuDescriptorHandle& inOutHandle){
    const Core::DescriptorWriteItem item = descriptorClass == Core::GpuDescriptorClass::UniformBuffer
        ? Core::DescriptorWriteItem::ConstantBuffer(0u, &buffer)
        : (writable ? Core::DescriptorWriteItem::StructuredBufferUav(0u, &buffer) : Core::DescriptorWriteItem::StructuredBufferSrv(0u, &buffer))
    ;
    return RendererDescriptorRegisterDetail::ReplaceDescriptor(heap, descriptorClass, item, inOutHandle);
}

void RetireHeapHandle(Core::GpuDescriptorHeap& heap, Core::GpuDescriptorHandle& handle){
    RendererDescriptorRegisterDetail::RetireDescriptor(heap, handle);
}

Core::ComputeState MakeComputePassState(const Core::ComputePipelineHandle& pipeline)noexcept{
    Core::ComputeState state;
    state.setPipeline(pipeline.get());
    // Set 0 is push-only; keep it so heap sets retain layout indices.
    return state;
}

// Binned-SAH BVH over indices[lo, hi); node 0 is root. Splits on the min SAH boundary, median on coincident centroids.
u32 BuildSceneBvhNode(const u32 lo, const u32 hi, const Span<const SceneBvhPrimitiveCalculation> primitiveBounds, const Span<const u32> instanceLeafCost, Span<u32> indices, Vector<SceneBvhNodeCalculation, Core::Alloc::ScratchArena>& nodes){
    return ::NWB::Impl::BuildSceneBvhNode(lo, hi, primitiveBounds, instanceLeafCost, indices, nodes);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
