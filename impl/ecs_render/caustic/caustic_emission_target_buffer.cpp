// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "caustic_emission_target_buffer.h"

#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/caustic/graph_types.adhd.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 CausticEmissionTargetBuffer::ensure(Core::GraphicsRuntime& graphics, usize targetCount){
    // Replace the heap slot before retiring the old emission-target buffer.
    auto& device = graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: caustic emission targets require the initialized global descriptor heap"));
        return false;
    }

    const auto acquireHeapHandle = [&](Core::Buffer& buffer) -> Expected<Core::GpuDescriptorHandle>{
        const auto handle = RayTracingDetail::RegisterHeapBuffer(heap, buffer, Core::GpuDescriptorClass::StorageBuffer, false);
        if(!handle){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register caustic emission targets in the descriptor heap"));
            return MakeUnexpected(Failure{});
        }
        return *handle;
    };

    if(buffer && this->capacity >= targetCount){
        if(heapHandle.valid()){
            NWB_ASSERT(heapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer);
            return true;
        }
        const auto handle = acquireHeapHandle(*buffer.get());
        if(!handle)
            return false;
        heapHandle = *handle;
        return true;
    }

    const usize capacity = ::NextGrowingCapacity(this->capacity, targetCount, s_CausticEmissionTargetInitialCapacity);

    Core::BufferDesc targetBufferDesc;
    targetBufferDesc
        .setByteSize(static_cast<u64>(sizeof(NwbCausticEmissionTargetGpu) * capacity))
        .setStructStride(sizeof(NwbCausticEmissionTargetGpu))
        .setDebugName(Name(s_RenderNameCausticEmissionTargets))
        // The upload and asynchronous photon reads share this immutable per-frame input.
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute).enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    auto targetBufferResult = graphics.createBuffer(targetBufferDesc);
    if(!targetBufferResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic emission-target buffer"));
        return false;
    }
    Core::BufferHandle targetBuffer = Move(*targetBufferResult);

    const auto targetHeapHandle = acquireHeapHandle(*targetBuffer.get());
    if(!targetHeapHandle)
        return false;

    Core::GpuDescriptorRetirementGuard retireUnpublished(heap, *targetHeapHandle);
    if(heapHandle.valid())
        heap.free(heapHandle);
    buffer = Move(targetBuffer);
    heapHandle = *targetHeapHandle;
    this->capacity = capacity;
    retireUnpublished.release();
    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created caustic emission-target buffer (capacity {} targets)"), static_cast<u64>(capacity));
    return true;
}

void CausticEmissionTargetBuffer::releaseDescriptor(Core::GraphicsRuntime& graphics){
    if(!heapHandle.valid())
        return;

    auto& device = graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(heap.isInitialized())
        heap.free(heapHandle);
    heapHandle = Core::GpuDescriptorHandle::Invalid();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

