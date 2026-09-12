// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "csg_system.h"

#include <impl/ecs_render/mesh/mesh_system.h>
#include <impl/ecs_render/mesh/mesh_view_private.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>
#include <impl/ecs_render/shared/renderer_push_constants_private.h>
#include <impl/ecs_render/csg/renderer_csg_state.h>

#include <impl/assets/graphics/csg/constants.h>

#include <core/common/log.h>
#include <core/graphics/runtime/runtime.h>
#include <impl/ecs_csg/module.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_resources{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ReserveCsgStructuredBuffer(
    Core::GraphicsRuntime& graphics,
    Core::BufferHandle& buffer,
    usize& inOutCapacity,
    const usize requiredCount,
    const usize elementByteSize,
    const Name& debugName
){
    if(requiredCount == 0u)
        return true;
    if(buffer && inOutCapacity >= requiredCount)
        return true;
    if(elementByteSize == 0u || requiredCount > Limit<usize>::s_Max / elementByteSize)
        return false;

    const usize capacity = ::NextGrowingCapacity(inOutCapacity, requiredCount);
    if(capacity > Limit<usize>::s_Max / elementByteSize)
        return false;

    Core::BufferDesc bufferDesc;
    bufferDesc
        .setByteSize(static_cast<u64>(capacity * elementByteSize))
        .setStructStride(static_cast<u32>(elementByteSize))
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setDebugName(debugName)
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;

    Core::BufferHandle createdBuffer = graphics.createBuffer(bufferDesc);
    if(!createdBuffer)
        return false;

    buffer = Move(createdBuffer);
    inOutCapacity = capacity;
    return true;
}

[[nodiscard]] static bool AcquireCsgBufferHeapHandle(
    Core::Device& device,
    Core::Buffer& buffer,
    const Core::GpuDescriptorClass::Enum descriptorClass,
    Core::GpuDescriptorHandle& outHandle
){
    outHandle = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized())
        return false;

    NWB_ASSERT(
        descriptorClass == Core::GpuDescriptorClass::StorageBuffer
        || descriptorClass == Core::GpuDescriptorClass::UniformBuffer
    );
    if(
        descriptorClass != Core::GpuDescriptorClass::StorageBuffer
        && descriptorClass != Core::GpuDescriptorClass::UniformBuffer
    )
        return false;
    const Core::DescriptorWriteItem descriptorWrite = descriptorClass == Core::GpuDescriptorClass::UniformBuffer
        ? Core::DescriptorWriteItem::ConstantBuffer(0u, &buffer)
        : Core::DescriptorWriteItem::StructuredBuffer_SRV(0u, &buffer)
    ;
    const Core::GpuDescriptorHandle acquired = heap.allocate(descriptorClass);
    if(!acquired.valid() || !heap.write(acquired, descriptorWrite)){
        if(acquired.valid())
            heap.free(acquired);
        return false;
    }

    outHandle = acquired;
    return true;
}

[[nodiscard]] static bool ReplaceCsgStorageBufferHeapHandle(
    Core::Device& device,
    Core::Buffer& buffer,
    Core::GpuDescriptorHandle& inOutHandle
){
    Core::GpuDescriptorHandle acquired;
    if(!AcquireCsgBufferHeapHandle(device, buffer, Core::GpuDescriptorClass::StorageBuffer, acquired)){
        // The buffer was replaced; drop the stale handle for a later retry.
        Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
        if(inOutHandle.valid() && heap.isInitialized())
            heap.free(inOutHandle);
        inOutHandle = Core::GpuDescriptorHandle::invalid();
        return false;
    }

    // Replacements take a fresh slot; the old one retires after quarantine.
    if(inOutHandle.valid())
        device.getDescriptorHeap().free(inOutHandle);
    inOutHandle = acquired;
    return true;
}

[[nodiscard]] static bool EnsureCsgBufferHeapHandle(
    Core::Device& device,
    Core::Buffer& buffer,
    const Core::GpuDescriptorClass::Enum descriptorClass,
    Core::GpuDescriptorHandle& inOutHandle
){
    if(inOutHandle.valid())
        return inOutHandle.descriptorClass() == descriptorClass;
    return AcquireCsgBufferHeapHandle(device, buffer, descriptorClass, inOutHandle);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererCsgSystem::createCsgClipResources(){
    auto& device = m_graphics.getDevice();
    if(!m_csgState.m_clipBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc(m_arena);
        bindingLayoutDesc.setVisibility(Core::ShaderType::Mesh | Core::ShaderType::Compute | Core::ShaderType::Pixel);
        // Push-only layout for the cap-fill path.
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(ECSRenderDetail::ShaderDrivenPushConstants)));

        m_csgState.m_clipBindingLayout = device.createBindingLayout(bindingLayoutDesc);
        if(!m_csgState.m_clipBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG clip binding layout"));
            return false;
        }
    }

    return true;
}

bool RendererCsgSystem::reserveCsgReceiverRangeBufferCapacity(const usize rangeCount){
    const usize oldCapacity = m_csgState.m_receiverRangeBufferCapacity;
    if(!__hidden_csg_resources::ReserveCsgStructuredBuffer(
        m_graphics,
        m_csgState.m_receiverRangeBuffer,
        m_csgState.m_receiverRangeBufferCapacity,
        rangeCount,
        sizeof(CsgReceiverRangeGpuData),
        ECSRenderDetail::s_CsgReceiverRangeBufferName
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG receiver range buffer"));
        return false;
    }

    if(m_csgState.m_receiverRangeBufferCapacity != oldCapacity && !__hidden_csg_resources::ReplaceCsgStorageBufferHeapHandle(
        m_graphics.getDevice(),
        *m_csgState.m_receiverRangeBuffer.get(),
        m_csgState.m_receiverRangeBufferHeapHandle
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register replacement CSG receiver range buffer in the descriptor heap"));
        return false;
    }
    return true;
}

bool RendererCsgSystem::reserveCsgCutterBufferCapacity(const usize cutterCount){
    const usize oldCapacity = m_csgState.m_cutterBufferCapacity;
    if(!__hidden_csg_resources::ReserveCsgStructuredBuffer(
        m_graphics,
        m_csgState.m_cutterBuffer,
        m_csgState.m_cutterBufferCapacity,
        cutterCount,
        sizeof(CsgCutterGpuData),
        ECSRenderDetail::s_CsgCutterBufferName
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG cutter buffer"));
        return false;
    }

    if(m_csgState.m_cutterBufferCapacity != oldCapacity && !__hidden_csg_resources::ReplaceCsgStorageBufferHeapHandle(
        m_graphics.getDevice(),
        *m_csgState.m_cutterBuffer.get(),
        m_csgState.m_cutterBufferHeapHandle
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register replacement CSG cutter buffer in the descriptor heap"));
        return false;
    }
    return true;
}

bool RendererCsgSystem::prepareCsgFrameResources(const usize receiverRangeCount, const usize cutterCount){
    if(receiverRangeCount == 0u || cutterCount == 0u)
        return true;
    NWB_ASSERT(receiverRangeCount <= static_cast<usize>(Limit<u32>::s_Max));
    NWB_ASSERT(cutterCount <= static_cast<usize>(Limit<u32>::s_Max));
    if(
        !reserveCsgReceiverRangeBufferCapacity(receiverRangeCount)
        || !reserveCsgCutterBufferCapacity(cutterCount)
    )
        return false;
    if(!createCsgIntervalSampleStateBuffer())
        return false;
    if(!m_csgState.m_clipContextSlotsBuffer){
        Core::BufferDesc bufferDesc;
        bufferDesc
            .setByteSize(sizeof(CsgClipContextSlots))
            .setIsConstantBuffer(true)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
            .setDebugName("engine/csg/clip_context_slots")
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        m_csgState.m_clipContextSlotsBuffer = m_graphics.createBuffer(bufferDesc);
        if(!m_csgState.m_clipContextSlotsBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG clip-context slot buffer"));
            return false;
        }
    }
    if(
        !__hidden_csg_resources::EnsureCsgBufferHeapHandle(
            m_graphics.getDevice(),
            *m_csgState.m_receiverRangeBuffer.get(),
            Core::GpuDescriptorClass::StorageBuffer,
            m_csgState.m_receiverRangeBufferHeapHandle
        )
        || !__hidden_csg_resources::EnsureCsgBufferHeapHandle(
            m_graphics.getDevice(),
            *m_csgState.m_cutterBuffer.get(),
            Core::GpuDescriptorClass::StorageBuffer,
            m_csgState.m_cutterBufferHeapHandle
        )
        || !__hidden_csg_resources::EnsureCsgBufferHeapHandle(
            m_graphics.getDevice(),
            *m_csgState.m_clipContextSlotsBuffer.get(),
            Core::GpuDescriptorClass::UniformBuffer,
            m_csgState.m_clipContextSlotsHeapHandle
        )
        || !__hidden_csg_resources::EnsureCsgBufferHeapHandle(
            m_graphics.getDevice(),
            *m_csgState.m_intervalSampleStateBuffer.get(),
            Core::GpuDescriptorClass::UniformBuffer,
            m_csgState.m_intervalSampleStateHeapHandle
        )
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: CSG clip context heap registration is incomplete"));
        return false;
    }
    if(!createCsgClipResources())
        return false;

    // Draw paths consume these handles only via a graph snapshot.
    NWB_ASSERT(m_csgState.m_receiverRangeBufferCapacity >= receiverRangeCount);
    NWB_ASSERT(m_csgState.m_cutterBufferCapacity >= cutterCount);
    NWB_ASSERT(m_csgState.m_receiverRangeBufferHeapHandle.valid());
    NWB_ASSERT(m_csgState.m_cutterBufferHeapHandle.valid());
    NWB_ASSERT(m_csgState.m_clipContextSlotsHeapHandle.valid());
    NWB_ASSERT(m_csgState.m_intervalSampleStateHeapHandle.valid());
    return true;
}

ECSRenderDetail::CsgGraphResourceSnapshot RendererCsgSystem::csgGraphResourceSnapshot()const{
    return {
        .receiverRanges = m_csgState.m_receiverRangeBuffer,
        .cutters = m_csgState.m_cutterBuffer,
        .clipContextSlots = m_csgState.m_clipContextSlotsBuffer,
        .intervalSampleState = m_csgState.m_intervalSampleStateBuffer,
        .receiverRangeCapacity = m_csgState.m_receiverRangeBufferCapacity,
        .cutterCapacity = m_csgState.m_cutterBufferCapacity,
        .receiverRangeHeapHandle = m_csgState.m_receiverRangeBufferHeapHandle,
        .cutterHeapHandle = m_csgState.m_cutterBufferHeapHandle,
        .clipContextSlotsHeapHandle = m_csgState.m_clipContextSlotsHeapHandle,
        .intervalSampleStateHeapHandle = m_csgState.m_intervalSampleStateHeapHandle,
    };
}

bool RendererCsgSystem::prepareCsgClipContextSlotData(
    const DeferredFrameTargets& targets,
    const CsgFrameGpuData& csgFrameData,
    const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources,
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings,
    CsgClipContextSlots& outContextSlots
)const{
    outContextSlots = CsgClipContextSlots{};
    if(!csgFrameData.hasWork())
        return true;
    if(
        !csgResources.frameReady(csgFrameData)
        || !frameBindings.bindingValid()
        || !targets.bindless.slotsBufferDescriptor.valid()
    )
        return false;

    // Freeze indirections now; later records must not see another generation.
    outContextSlots.receiverRanges = csgResources.receiverRangeHeapHandle.slot();
    outContextSlots.cutters = csgResources.cutterHeapHandle.slot();
    outContextSlots.materialTyped = frameBindings.materialTypedHeapHandle.slot();
    outContextSlots.meshInstances = frameBindings.instanceHeapHandle.slot();
    outContextSlots.deferredBindlessResources = targets.bindless.slotsBufferDescriptor.slot();
    outContextSlots.intervalSampleState = csgResources.intervalSampleStateHeapHandle.slot();
    return true;
}

void RendererCsgSystem::setCsgReceiverSurfaceImageStates(Core::CommandList& commandList, const DeferredFrameTargets& targets){
    commandList.setTextureState(targets.csgReceiverEventData.get(), Core::s_AllSubresources, Core::ResourceStates::UnorderedAccess);
    commandList.setTextureState(targets.csgReceiverEventCount.get(), Core::s_AllSubresources, Core::ResourceStates::UnorderedAccess);
}

void RendererCsgSystem::setCsgIntervalSampleImageStates(Core::CommandList& commandList, const DeferredFrameTargets& targets){
    commandList.setTextureState(targets.csgRemovedIntervalDepth.get(), Core::s_AllSubresources, Core::ResourceStates::UnorderedAccess);
    commandList.setTextureState(targets.csgRemovedIntervalCapNormal.get(), Core::s_AllSubresources, Core::ResourceStates::UnorderedAccess);
    commandList.setTextureState(targets.csgRemovedIntervalData.get(), Core::s_AllSubresources, Core::ResourceStates::UnorderedAccess);
    commandList.setTextureState(targets.csgRemovedIntervalCount.get(), Core::s_AllSubresources, Core::ResourceStates::UnorderedAccess);
}

void RendererCsgSystem::setCsgClipBufferStates(
    Core::CommandList& commandList,
    const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources
){
    commandList.setBufferState(csgResources.receiverRanges.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(csgResources.cutters.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(csgResources.clipContextSlots.get(), Core::ResourceStates::ConstantBuffer);
    commandList.setBufferState(csgResources.intervalSampleState.get(), Core::ResourceStates::ConstantBuffer);
}


void RendererCsgSystem::releaseCsgClipContextHeapHandles(){
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(heap.isInitialized()){
        heap.free(m_csgState.m_receiverRangeBufferHeapHandle);
        heap.free(m_csgState.m_cutterBufferHeapHandle);
        heap.free(m_csgState.m_clipContextSlotsHeapHandle);
        heap.free(m_csgState.m_intervalSampleStateHeapHandle);
    }
    m_csgState.m_receiverRangeBufferHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_csgState.m_cutterBufferHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_csgState.m_clipContextSlotsHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_csgState.m_intervalSampleStateHeapHandle = Core::GpuDescriptorHandle::invalid();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

