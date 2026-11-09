// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "mesh_system.h"
#include "mesh_swbvh_bindings.h"
#include "mesh_emulation_resources.h"
#include <impl/assets/graphics/mesh/runtime_bounds_constants.h>
#include <impl/ecs_render/kernel/descriptor_register_utils.h>
#include <impl/ecs_render/mesh/instance_gpu_data.adhd.h>
#include <impl/ecs_render/mesh/renderer_mesh_state.h>
#include <impl/ecs_render/shared/renderer_push_constants_private.h>
#include <core/common/log.h>
#include <core/graphics/rhi/gpu_descriptor_heap.h>
#include <core/graphics/runtime/runtime.h>
#include <global/span.h>
#include "mesh_descriptor_transaction.h"
#include <impl/ecs_render/mesh/mesh_geometry_buffer_policy.adhd.h>
#include <core/graphics/backend_selection.h>
#include "mesh_frame_bindings.h"
#include "object_geometry_cache.h"
#include <impl/assets/graphics/mesh/object_geometry_constants.h>
#include <impl/ecs_render/mesh/mesh_view_state_policy.adhd.h>
#include "mesh_view_private.h"
#include "task_graph_prefix_tasks.h"
#include <impl/ecs_render/kernel/task_graph_queue_lookup.h>
#include <impl/ecs_render/kernel/timing_names.h>
#include <core/graphics/gpu_timing.h>
#include <core/task/gpu/compiled_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererMeshSystem::prepareComputeEmulationResources(MeshResources& mesh){
    return MeshEmulationResources{}.prepare(m_graphics, mesh);
}

b8 RendererMeshSystem::prepareMeshFrameBindings(const ECSRenderDetail::MaterialPassBufferSnapshot& materialBuffers){
    return m_meshState.m_frameBindings.prepare(m_graphics, materialBuffers, m_meshState.m_meshViewBuffer);
}

ECSRenderDetail::MeshFrameBindingSnapshot RendererMeshSystem::meshFrameBindingSnapshot()const noexcept{
    return m_meshState.m_frameBindings.snapshot(m_meshState.m_meshViewBuffer);
}

void RendererMeshSystem::populateMeshGeometryHeapSlots(const MeshResources& mesh, InstanceGpuData& outInstance)const noexcept{
    NWB_ASSERT(GeometryHeapHandlesReady(mesh.geometryHeapHandles));
    for(u32 slotIndex = 0u; slotIndex < NWB_MESH_INSTANCE_GEOMETRY_SLOT_COUNT; ++slotIndex)
        outInstance.geometryHeapSlots[slotIndex] = 0u;
    ForEachMeshSourceBindingSlot([&](const u32 bindingSlot, const b8)noexcept{
        outInstance.geometryHeapSlots[bindingSlot] = mesh.geometryHeapHandles[bindingSlot].slot();
    });
}

void RendererMeshSystem::releaseMeshGeometryHeapHandles(MeshResources& mesh){
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(heap.isInitialized()){
        for(Core::GpuDescriptorHandle& handle : mesh.geometryHeapHandles){
            if(handle.valid())
                heap.free(handle);
            handle = Core::GpuDescriptorHandle::Invalid();
        }
        if(mesh.swBvhPositionHeapHandle.valid())
            heap.free(mesh.swBvhPositionHeapHandle);
        if(mesh.swBvhTriangleIndexHeapHandle.valid())
            heap.free(mesh.swBvhTriangleIndexHeapHandle);
        if(mesh.swBvhNodeHeapHandle.valid())
            heap.free(mesh.swBvhNodeHeapHandle);
        if(mesh.swBvhParentHeapHandle.valid())
            heap.free(mesh.swBvhParentHeapHandle);
        if(mesh.emulationVertexHeapHandle.valid())
            heap.free(mesh.emulationVertexHeapHandle);
        if(mesh.runtimeLocalBoundsHeapHandle.valid())
            heap.free(mesh.runtimeLocalBoundsHeapHandle);
        if(mesh.runtimeMeshletLocalBoundsHeapHandle.valid())
            heap.free(mesh.runtimeMeshletLocalBoundsHeapHandle);
        if(mesh.objectGeometryCache.heapHandle.valid())
            heap.free(mesh.objectGeometryCache.heapHandle);
        mesh.swBvhPositionHeapHandle = Core::GpuDescriptorHandle::Invalid();
        mesh.swBvhTriangleIndexHeapHandle = Core::GpuDescriptorHandle::Invalid();
        mesh.swBvhNodeHeapHandle = Core::GpuDescriptorHandle::Invalid();
        mesh.swBvhParentHeapHandle = Core::GpuDescriptorHandle::Invalid();
        mesh.emulationVertexHeapHandle = Core::GpuDescriptorHandle::Invalid();
        mesh.runtimeLocalBoundsHeapHandle = Core::GpuDescriptorHandle::Invalid();
        mesh.runtimeMeshletLocalBoundsHeapHandle = Core::GpuDescriptorHandle::Invalid();
        mesh.objectGeometryCache.heapHandle = Core::GpuDescriptorHandle::Invalid();
        mesh.objectGeometryCache.acceptedContent = false;
        return;
    }

    for(Core::GpuDescriptorHandle& handle : mesh.geometryHeapHandles)
        handle = Core::GpuDescriptorHandle::Invalid();
    mesh.swBvhPositionHeapHandle = Core::GpuDescriptorHandle::Invalid();
    mesh.swBvhTriangleIndexHeapHandle = Core::GpuDescriptorHandle::Invalid();
    mesh.swBvhNodeHeapHandle = Core::GpuDescriptorHandle::Invalid();
    mesh.swBvhParentHeapHandle = Core::GpuDescriptorHandle::Invalid();
    mesh.emulationVertexHeapHandle = Core::GpuDescriptorHandle::Invalid();
    mesh.runtimeLocalBoundsHeapHandle = Core::GpuDescriptorHandle::Invalid();
    mesh.runtimeMeshletLocalBoundsHeapHandle = Core::GpuDescriptorHandle::Invalid();
    mesh.objectGeometryCache.heapHandle = Core::GpuDescriptorHandle::Invalid();
    mesh.objectGeometryCache.acceptedContent = false;
}

void RendererMeshSystem::releaseMeshFrameHeapHandles(){
    m_meshState.m_frameBindings.release(m_graphics);
}

b8 RendererMeshSystem::createMeshRenderBindings(MeshResources& mesh){
    if(!createMeshGeometryHeapHandles(mesh))
        return false;

    if(mesh.runtimeLocalBoundsBuffer && !mesh.runtimeLocalBoundsHeapHandle.valid()){
        auto& device = m_graphics.getDevice();
        Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
        const Core::BufferDesc& desc = mesh.runtimeLocalBoundsBuffer->getCreationDescription();
        if(!heap.isInitialized() || desc.byteSize < NWB_RUNTIME_MESH_BOUNDS_BYTE_SIZE || !desc.canHaveRawViews){
            releaseMeshGeometryHeapHandles(mesh);
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: runtime local bounds require a complete raw GPU buffer"));
            return false;
        }
        if(!RendererDescriptorRegisterDetail::AssignDescriptor(heap, Core::GpuDescriptorClass::StorageBuffer, Core::DescriptorWriteItem::RawBufferSrv(0u, mesh.runtimeLocalBoundsBuffer.get()), mesh.runtimeLocalBoundsHeapHandle)){
            releaseMeshGeometryHeapHandles(mesh);
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register runtime local bounds"));
            return false;
        }
    }

    if(mesh.runtimeMesh && mesh.runtimeGeometryContentRevision != 0u && mesh.runtimeMeshletLocalBoundsBuffer && !mesh.runtimeMeshletLocalBoundsHeapHandle.valid()){
        Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
        const Core::BufferDesc& desc = mesh.runtimeMeshletLocalBoundsBuffer->getCreationDescription();
        const u64 requiredBytes = static_cast<u64>(mesh.meshletCount) * NWB_RUNTIME_MESH_BOUNDS_BYTE_SIZE;
        if(!(heap.isInitialized() && requiredBytes != 0u && desc.byteSize >= requiredBytes && desc.canHaveRawViews
            && RendererDescriptorRegisterDetail::AssignDescriptor(heap, Core::GpuDescriptorClass::StorageBuffer, Core::DescriptorWriteItem::RawBufferSrv(0u, mesh.runtimeMeshletLocalBoundsBuffer.get()), mesh.runtimeMeshletLocalBoundsHeapHandle)))
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: mesh '{}' could not register optional meshlet bounds; retaining whole-caster shadows"), ConvertString(mesh.meshName.resolvedText()));
    }

    NWB_ASSERT(meshRenderBindingsReady(mesh));
    return true;
}

b8 RendererMeshSystem::meshRenderBindingsReady(const MeshResources& mesh)const noexcept{
    if(!mesh.valid() || !GeometryHeapHandlesReady(mesh.geometryHeapHandles))
        return false;

    if(mesh.runtimeLocalBoundsBuffer && (!mesh.runtimeLocalBoundsHeapHandle.valid() || mesh.runtimeLocalBoundsHeapHandle.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer))
        return false;

    return true;
}

b8 RendererMeshSystem::createMeshGeometryHeapHandles(MeshResources& mesh){
    if(GeometryHeapHandlesReady(mesh.geometryHeapHandles))
        return true;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh '{}' requires the initialized global descriptor heap"), ConvertString(mesh.meshName.resolvedText()));
        return false;
    }

    // A non-empty partial set signals a broken lifetime transition, never a safe merge.
#if defined(NWB_DEBUG)
    for(const Core::GpuDescriptorHandle handle : mesh.geometryHeapHandles)
        NWB_ASSERT(!handle.valid());
#endif

    Array<Core::GpuDescriptorHandle, NWB_MESH_INSTANCE_GEOMETRY_SLOT_COUNT> acquired{};
    Core::GpuDescriptorRetirementGuard retireAcquired(heap, acquired);
    b8 registered = true;
    ForEachMeshSourceBuffer(mesh, [&](const u32 bindingSlot, const Core::BufferHandle& buffer, const b8){
        if(!registered)
            return;
        if(!buffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh '{}' has no source buffer at binding {}"), ConvertString(mesh.meshName.resolvedText()), bindingSlot);
            registered = false;
            return;
        }

        if(!RendererDescriptorRegisterDetail::AssignDescriptor(heap, Core::GpuDescriptorClass::StorageBuffer, Core::DescriptorWriteItem::StructuredBufferSrv(0u, buffer.get()), acquired[bindingSlot]))
            registered = false;
    });

    if(!registered)
        return false;

    ForEachMeshSourceBindingSlot([&](const u32 bindingSlot, const b8)noexcept{
        mesh.geometryHeapHandles[bindingSlot] = acquired[bindingSlot];
    });
    retireAcquired.release();
    NWB_ASSERT(GeometryHeapHandlesReady(mesh.geometryHeapHandles));
    return true;
}

b8 RendererMeshSystem::ensureMeshSwBvhInputHeapHandles(MeshResources& mesh){
    return MeshSoftwareBvhBindings{}.ensure(m_graphics, mesh);
}

void RendererMeshSystem::releaseAllMeshGeometryHeapHandles(){
    for(auto it = m_meshState.m_meshes.begin(); it != m_meshState.m_meshes.end(); ++it)
        releaseMeshGeometryHeapHandles(it.value());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


MeshDescriptorTransaction::MeshDescriptorTransaction(RendererMeshSystem& meshSystem, MeshResources& mesh)noexcept
    : m_meshSystem(meshSystem)
    , m_mesh(mesh)
{}

MeshDescriptorTransaction::~MeshDescriptorTransaction()noexcept(false){
    if(m_active)
        m_meshSystem.releaseMeshGeometryHeapHandles(m_mesh);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 MeshEmulationResources::prepare(Core::GraphicsRuntime& graphics, MeshResources& mesh)const{
    if(mesh.emulationVertexHeapHandle.valid())
        return true;
    if(!mesh.emulationVertexBuffer){
        const Expected<ComputeEmulationLayout> layoutResult = MeshGeometryBufferPolicy().resolveComputeEmulationLayout(mesh.meshletLocalVertexRefBuffer->getDescription().byteSize, mesh.meshletPrimitiveIndexCount);
        if(!layoutResult){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: generated geometry layout exceeds the index byte-address range for mesh '{}'"), ConvertString(mesh.meshName.resolvedText()));
            return false;
        }
        const ComputeEmulationLayout& layout = *layoutResult;
        const auto emulationVertexBufferName = DeriveName(mesh.meshName, AStringView(":emulation_vb"));
        if(!emulationVertexBufferName){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to derive compute-emulation vertex buffer name for mesh '{}'"), ConvertString(mesh.meshName.resolvedText()));
            return false;
        }

        Core::BufferDesc emulationVertexBufferDesc;
        emulationVertexBufferDesc
            .setByteSize(layout.bufferByteSize)
            .setStructStride(s_EmulatedVertexStride)
            .setCanHaveRawViews(true)
            .setCanHaveUAVs(true)
            .setIsVertexBuffer(true)
            .setIsIndexBuffer(true)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
            .setDebugName(*emulationVertexBufferName)
        ;
        auto emulationVertexBufferResult = graphics.createBuffer(emulationVertexBufferDesc);
        if(emulationVertexBufferResult)
            mesh.emulationVertexBuffer = Move(*emulationVertexBufferResult);
        else
            mesh.emulationVertexBuffer.reset();
        if(!emulationVertexBufferResult){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create compute-emulation vertex buffer for mesh '{}'"), ConvertString(mesh.meshName.resolvedText()));
            return false;
        }
        mesh.emulationIndexByteOffset = layout.indexByteOffset;
    }

    auto& device = graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: compute-emulation vertex buffer requires the initialized global descriptor heap"));
        return false;
    }
    if(RendererDescriptorRegisterDetail::AssignDescriptor(heap, Core::GpuDescriptorClass::StorageBuffer, Core::DescriptorWriteItem::StructuredBufferUav(0u, mesh.emulationVertexBuffer.get()), mesh.emulationVertexHeapHandle))
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register compute-emulation vertex buffer in the descriptor heap for mesh '{}'"), ConvertString(mesh.meshName.resolvedText()));
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 MeshFrameBindings::prepare(Core::GraphicsRuntime& graphics, const ECSRenderDetail::MaterialPassBufferSnapshot& materialBuffers, const Core::BufferHandle& meshViewBuffer){
    if(m_snapshot.matches(materialBuffers, meshViewBuffer))
        return true;

    if(!materialBuffers.valid() || !meshViewBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: frame heap registration requires instance, typed-material, and view buffers"));
        return false;
    }

    auto& device = graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: frame heap registration requires the initialized global descriptor heap"));
        return false;
    }
    const Expected<Core::GpuDescriptorHandle> instanceHandleResult = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
    if(!instanceHandleResult)
        return false;
    const Core::GpuDescriptorHandle instanceHandle = *instanceHandleResult;
    Core::GpuDescriptorRetirementGuard retireInstance(heap, instanceHandle);
    const Expected<Core::GpuDescriptorHandle> materialTypedHandleResult = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
    if(!materialTypedHandleResult)
        return false;
    const Core::GpuDescriptorHandle materialTypedHandle = *materialTypedHandleResult;
    Core::GpuDescriptorRetirementGuard retireMaterialTyped(heap, materialTypedHandle);
    const Expected<Core::GpuDescriptorHandle> viewHandleResult = heap.allocate(Core::GpuDescriptorClass::UniformBuffer);
    if(!viewHandleResult)
        return false;
    const Core::GpuDescriptorHandle viewHandle = *viewHandleResult;
    Core::GpuDescriptorRetirementGuard retireView(heap, viewHandle);
    const b8 registered =
        heap.write(instanceHandle, Core::DescriptorWriteItem::StructuredBufferSrv(0u, materialBuffers.instanceBuffer.get()))
        && heap.write(materialTypedHandle, Core::DescriptorWriteItem::StructuredBufferSrv(0u, materialBuffers.materialTypedBuffer.get()))
        && heap.write(viewHandle, Core::DescriptorWriteItem::ConstantBuffer(0u, meshViewBuffer.get()))
    ;
    if(!registered)
        return false;

    ECSRenderDetail::MeshFrameBindingSnapshot replacement = {
        .instanceBuffer = materialBuffers.instanceBuffer,
        .materialTypedBuffer = materialBuffers.materialTypedBuffer,
        .meshView = { .buffer = meshViewBuffer, .heapHandle = viewHandle, },
        .instanceHeapHandle = instanceHandle,
        .materialTypedHeapHandle = materialTypedHandle,
        .instanceBufferCapacity = materialBuffers.instanceBufferCapacity,
        .materialTypedBufferCapacity = materialBuffers.materialTypedBufferCapacity,
    };
    ECSRenderDetail::MeshFrameBindingSnapshot retired = Move(m_snapshot);
    m_snapshot = Move(replacement);
    retireInstance.release();
    retireMaterialTyped.release();
    retireView.release();

    // Keep retired buffers alive until descriptors enter deferred-reuse quarantine.
    if(retired.instanceHeapHandle.valid())
        heap.free(retired.instanceHeapHandle);
    if(retired.materialTypedHeapHandle.valid())
        heap.free(retired.materialTypedHeapHandle);
    if(retired.meshView.heapHandle.valid())
        heap.free(retired.meshView.heapHandle);

    NWB_ASSERT(m_snapshot.matches(materialBuffers, meshViewBuffer));
    return true;
}

ECSRenderDetail::MeshFrameBindingSnapshot MeshFrameBindings::snapshot(const Core::BufferHandle& meshViewBuffer)const noexcept{
    const ECSRenderDetail::MeshFrameBindingSnapshot& bindings = m_snapshot;
    return {
        .instanceBuffer = bindings.instanceBuffer,
        .materialTypedBuffer = bindings.materialTypedBuffer,
        .meshView = viewSnapshot(meshViewBuffer),
        .instanceHeapHandle = bindings.instanceHeapHandle,
        .materialTypedHeapHandle = bindings.materialTypedHeapHandle,
        .instanceBufferCapacity = bindings.instanceBufferCapacity,
        .materialTypedBufferCapacity = bindings.materialTypedBufferCapacity,
    };
}

ECSRenderDetail::MeshViewBufferSnapshot MeshFrameBindings::viewSnapshot(const Core::BufferHandle& meshViewBuffer)const noexcept{
    ECSRenderDetail::MeshViewBufferSnapshot snapshot;
    snapshot.buffer = meshViewBuffer;
    if(m_snapshot.meshView.buffer == meshViewBuffer)
        snapshot.heapHandle = m_snapshot.meshView.heapHandle;
    return snapshot;
}

void MeshFrameBindings::release(Core::GraphicsRuntime& graphics){
    auto& device = graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(heap.isInitialized()){
        if(m_snapshot.instanceHeapHandle.valid())
            heap.free(m_snapshot.instanceHeapHandle);
        if(m_snapshot.materialTypedHeapHandle.valid())
            heap.free(m_snapshot.materialTypedHeapHandle);
        if(m_snapshot.meshView.heapHandle.valid())
            heap.free(m_snapshot.meshView.heapHandle);
    }
    m_snapshot = {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 MeshSoftwareBvhBindings::ensure(Core::GraphicsRuntime& graphics, MeshResources& mesh)const{
    const auto inputsReady = [&mesh]()noexcept{
        return
            mesh.swBvhPositionHeapHandle.valid()
            && mesh.swBvhPositionHeapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer
            && mesh.swBvhTriangleIndexHeapHandle.valid()
            && mesh.swBvhTriangleIndexHeapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer
        ;
    };
    if(inputsReady())
        return true;

    NWB_ASSERT(!mesh.swBvhPositionHeapHandle.valid());
    NWB_ASSERT(!mesh.swBvhTriangleIndexHeapHandle.valid());
    if(!mesh.positionBuffer || !mesh.triangleIndexBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh '{}' has no software-BVH position or triangle-index input"), ConvertString(mesh.meshName.resolvedText()));
        return false;
    }

    auto& device = graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh '{}' requires the initialized global descriptor heap for software-BVH inputs"), ConvertString(mesh.meshName.resolvedText()));
        return false;
    }

    const auto registrationFailed = [&mesh](){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register software-BVH inputs for mesh '{}'"), ConvertString(mesh.meshName.resolvedText()));
        return false;
    };
    const Expected<Core::GpuDescriptorHandle> positionHandleResult = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
    if(!positionHandleResult)
        return registrationFailed();
    const Core::GpuDescriptorHandle positionHandle = *positionHandleResult;
    Core::GpuDescriptorRetirementGuard retirePosition(heap, positionHandle);
    const Expected<Core::GpuDescriptorHandle> triangleIndexHandleResult = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
    if(!triangleIndexHandleResult)
        return registrationFailed();
    const Core::GpuDescriptorHandle triangleIndexHandle = *triangleIndexHandleResult;
    Core::GpuDescriptorRetirementGuard retireTriangleIndex(heap, triangleIndexHandle);
    if(!heap.write(positionHandle, Core::DescriptorWriteItem::RawBufferSrv(0u, mesh.positionBuffer.get())) || !heap.write(triangleIndexHandle, Core::DescriptorWriteItem::RawBufferSrv(0u, mesh.triangleIndexBuffer.get())))
        return registrationFailed();

    mesh.swBvhPositionHeapHandle = positionHandle;
    mesh.swBvhTriangleIndexHeapHandle = triangleIndexHandle;
    retirePosition.release();
    retireTriangleIndex.release();
    NWB_ASSERT(inputsReady());
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 AcceptObjectGeometryCacheWrite(MeshResources& mesh, const RuntimeMeshBuffers& sourceBuffers, const ObjectGeometryCacheSnapshot& expected, const b8 runtimeMesh)noexcept{
    ObjectGeometryCacheState& cache = mesh.objectGeometryCache;
    if(!expected.valid() || cache.buffer != expected.buffer)
        return false;
    // Accepted writes establish the retained buffer state even if current source content already moved on.
    return MeshGeometryBufferPolicy().acceptObjectCacheContent(
        cache.decoderPipeline == expected.decoderPipeline,
        cache.heapHandle == expected.heapHandle,
        cache.indexByteOffset,
        expected.indexByteOffset,
        cache.indexCount,
        expected.indexCount,
        mesh.runtimeMesh,
        runtimeMesh,
        mesh.runtimeGeometryContentRevision,
        expected.sourceRevision,
        mesh.sameBuffers(sourceBuffers),
        cache.initialized,
        cache.acceptedContent,
        cache.acceptedContentRevision
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ECSRenderDetail::ObjectGeometryCacheSnapshot RendererMeshSystem::ObjectGeometryCacheSnapshot(const MeshResources& mesh)noexcept{
    const ECSRenderDetail::ObjectGeometryCacheState& cache = mesh.objectGeometryCache;
    return {
        .buffer = cache.buffer,
        .decoderPipeline = cache.decoderPipeline,
        .sourceRevision = mesh.runtimeGeometryContentRevision,
        .heapHandle = cache.heapHandle,
        .indexByteOffset = cache.indexByteOffset,
        .indexCount = cache.indexCount,
        .initialized = cache.initialized,
        .requiresDecode = MeshGeometryBufferPolicy().requiresObjectCacheDecode(cache.acceptedContent, mesh.runtimeMesh, mesh.runtimeGeometryContentRevision, cache.acceptedContentRevision),
    };
}

b8 RendererMeshSystem::prepareObjectGeometryCache(MeshResources& mesh, const Core::ComputePipelineHandle& decoderPipeline){
    if(!decoderPipeline)
        return false;
    ECSRenderDetail::ObjectGeometryCacheState& cache = mesh.objectGeometryCache;
    if(cache.decoderPipeline != decoderPipeline){
        cache.decoderPipeline = decoderPipeline;
        cache.acceptedContent = false;
    }
    if(cache.buffer && cache.heapHandle.valid())
        return true;
    if(!cache.buffer){
        if(!mesh.meshletLocalVertexRefBuffer)
            return false;
        const Expected<ObjectGeometryCacheLayout> layoutResult = MeshGeometryBufferPolicy().resolveObjectCacheLayout(mesh.meshletLocalVertexRefBuffer->getDescription().byteSize, mesh.meshletPrimitiveIndexCount);
        if(!layoutResult)
            return false;
        const ObjectGeometryCacheLayout& layout = *layoutResult;
        const auto bufferName = DeriveName(mesh.meshName, AStringView(":object_geometry"));
        if(!bufferName)
            return false;
        Core::BufferDesc desc;
        desc
            .setByteSize(layout.bufferByteSize)
            .setStructStride(NWB_MESH_OBJECT_VERTEX_BYTE_SIZE)
            .setCanHaveRawViews(true)
            .setCanHaveUAVs(true)
            .setIsVertexBuffer(true)
            .setIsIndexBuffer(true)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
            .setDebugName(*bufferName)
        ;
        auto bufferResult = m_graphics.createBuffer(desc);
        if(bufferResult)
            cache.buffer = Move(*bufferResult);
        else
            cache.buffer.reset();
        if(!bufferResult){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create object geometry cache for mesh '{}'"), ConvertString(mesh.meshName.resolvedText()));
            return false;
        }
        cache.indexByteOffset = layout.indexByteOffset;
        cache.indexCount = layout.indexCount;
        cache.acceptedContent = false;
        cache.initialized = false;
    }
    auto& heap = m_graphics.getDevice().getDescriptorHeap();
    if(!heap.isInitialized())
        return false;
    return RendererDescriptorRegisterDetail::AssignDescriptor(heap, Core::GpuDescriptorClass::StorageBuffer, Core::DescriptorWriteItem::StructuredBufferUav(0u, cache.buffer.get()), cache.heapHandle);
}

b8 RendererMeshSystem::confirmObjectGeometryCache(const Name& meshKey, const RuntimeMeshBuffers& sourceBuffers, const ECSRenderDetail::ObjectGeometryCacheSnapshot& expected, const b8 runtimeMesh)noexcept{
    const auto mesh = findMeshResources(meshKey);
    if(!mesh)
        return false;
    return ECSRenderDetail::AcceptObjectGeometryCacheWrite(**mesh, sourceBuffers, expected, runtimeMesh);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


MeshViewGpuData ResolveMeshViewState(const f32 fallbackAspectRatio, Core::ECS::World& world)noexcept{
    SIMDMatrix viewBasis = NWB::Impl::Scene::BuildDefaultSceneViewBasis();
    NWB::Impl::Scene::CameraProjection projection = NWB::Impl::Scene::StoreDefaultCameraProjection(fallbackAspectRatio);

    const NWB::Impl::Scene::SceneCameraView cameraView = NWB::Impl::Scene::ResolveSceneCameraView(world, fallbackAspectRatio);
    if(cameraView.valid()){
        viewBasis = NWB::Impl::Scene::BuildSceneViewBasis(LoadFloat(cameraView.transform->position), LoadFloat(cameraView.transform->rotation));
        projection = cameraView.projection;
    }

    return MeshViewStatePolicy().build(
        viewBasis.v[0u],
        viewBasis.v[1u],
        viewBasis.v[2u],
        viewBasis.v[3u],
        LoadFloat(projection.projectionParams),
        projection.tanHalfVerticalFov,
        projection.aspectRatio,
        projection.nearPlane,
        projection.farPlane
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererMeshState::invalidateResources()noexcept{
    m_meshes.clear();
    m_meshViewBuffer.reset();
    m_frameBindings.reset();
    m_meshViewGpuDataValid = false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const Core::BufferHandle& RendererMeshSystem::MeshSourceBuffer(const MeshResources& mesh, const u32 bindingSlot)noexcept{
    switch(bindingSlot){
    case NWB_MESH_BINDING_POSITION: return mesh.positionBuffer;
    case NWB_MESH_BINDING_NORMAL: return mesh.normalBuffer;
    case NWB_MESH_BINDING_TANGENT: return mesh.tangentBuffer;
    case NWB_MESH_BINDING_UV0: return mesh.uv0Buffer;
    case NWB_MESH_BINDING_COLOR: return mesh.colorBuffer;
    case NWB_MESH_BINDING_MESHLET_DESC: return mesh.meshletDescBuffer;
    case NWB_MESH_BINDING_MESHLET_BOUNDS: return mesh.meshletBoundsBuffer;
    case NWB_MESH_BINDING_MESHLET_POSITION_REFS: return mesh.meshletPositionRefDeltaBuffer;
    case NWB_MESH_BINDING_MESHLET_ATTRIBUTE_REFS: return mesh.meshletAttributeRefDeltaBuffer;
    case NWB_MESH_BINDING_MESHLET_LOCAL_VERTEX_REFS: return mesh.meshletLocalVertexRefBuffer;
    case NWB_MESH_BINDING_MESHLET_PRIMITIVE_INDICES: return mesh.meshletPrimitiveIndexBuffer;
    default:
        NWB_ASSERT(false);
        return mesh.positionBuffer;
    }
}

void RendererMeshSystem::invalidateResources(){
    releaseAllMeshGeometryHeapHandles();
    releaseMeshFrameHeapHandles();
    m_meshState.invalidateResources();
}

b8 RendererMeshSystem::createMeshViewBuffer(){
    if(m_meshState.m_meshViewBuffer)
        return true;

    Core::BufferDesc meshViewBufferDesc;
    meshViewBufferDesc
        .setByteSize(sizeof(MeshViewGpuData))
        .setIsConstantBuffer(true)
        .setDebugName(s_MeshViewBufferName)
        // Either caustic producer can consume the view; keep concurrent to avoid serializing lanes.
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute).enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    auto meshViewBufferResult = m_graphics.createBuffer(meshViewBufferDesc);
    if(!meshViewBufferResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create mesh view buffer"));
        return false;
    }
    Core::BufferHandle meshViewBuffer = Move(*meshViewBufferResult);

    releaseMeshFrameHeapHandles();
    m_meshState.m_meshViewBuffer = Move(meshViewBuffer);
    return true;
}

ECSRenderDetail::MeshViewBufferSnapshot RendererMeshSystem::meshViewBufferSnapshot()const noexcept{
    return m_meshState.m_frameBindings.viewSnapshot(m_meshState.m_meshViewBuffer);
}

Expected<Float44> RendererMeshSystem::snapshotAcceptedMeshViewWorldToClip()const noexcept{
    const Expected<MeshViewGpuData> acceptedView = MeshViewStatePolicy().readAccepted(m_meshState.m_meshViewGpuDataValid, m_meshState.m_meshViewGpuData);
    if(!acceptedView)
        return MakeUnexpected(Failure{});
    return acceptedView->worldToClip;
}

MeshViewBufferUpload RendererMeshSystem::prepareMeshViewBufferUpload(const f32 fallbackAspectRatio)const noexcept{
    NWB_ASSERT(m_meshState.m_meshViewBuffer);

    MeshViewBufferUpload upload;
    upload.viewState = ECSRenderDetail::ResolveMeshViewState(fallbackAspectRatio, m_world);
    upload.uploadRequired = MeshViewStatePolicy().uploadRequired(m_meshState.m_meshViewGpuDataValid, m_meshState.m_meshViewGpuData, upload.viewState);
    return upload;
}

void RendererMeshSystem::confirmMeshViewBufferUpload(const MeshViewGpuData& viewState)noexcept{
    MeshViewStatePolicy().storeConfirmed(viewState, m_meshState.m_meshViewGpuData, m_meshState.m_meshViewGpuDataValid);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 MeshViewSetupGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
    const Core::GpuPhysicalQueueInfo* const shadowVisibilityQueue = ECSRenderDetail::QueueForTask(context, payload.shadowVisibilityTask);
    if(!shadowVisibilityQueue)
        return false;

    const b8 shadowVisibilityRunsOnCompute = shadowVisibilityQueue->queueClass == Core::CommandQueue::Compute;
    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
    const b8 recordsGraphicsFrameMarker = !shadowVisibilityRunsOnCompute && RendererGpuTimingScope::s_Frame.valid();
    if(recordsGraphicsFrameMarker)
        commandList.beginMarker(RendererGpuTimingScope::s_Frame.markerLabel);

    if(shadowVisibilityRunsOnCompute && payload.asyncPrefixTimingSpansOnePacket){
        payload.asyncPrefixTiming.emplace(payload.graphics.gpuTiming(), RendererGpuTimingScope::s_AsyncPrefix, payload.graphics.getDevice(), commandList);
        if(!Core::FinishSplitGpuTimingMarker(&payload.asyncPrefixTiming))
            return false;
    }

    if(recordsGraphicsFrameMarker)
        commandList.endMarker();
    return true;
}

b8 MeshViewUploadCommitGraphTask::Record(const Payload& payload, [[maybe_unused]] Core::CommandList& commandList, [[maybe_unused]] const Core::GpuTaskRecordContext& context)noexcept{
    payload.ready = true;
    return true;
}

void MeshViewUploadCommitGraphTask::Accepted(Payload& payload, [[maybe_unused]] const Core::QueueSubmissionToken& token)noexcept{
    if(payload.uploadRequired)
        payload.meshSystem.confirmMeshViewBufferUpload(payload.viewState);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
