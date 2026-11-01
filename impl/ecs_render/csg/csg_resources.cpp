// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "csg_system.h"

#include <impl/ecs_render/deferred/deferred_frame_targets.h>
#include <impl/ecs_render/shared/renderer_push_constants_private.h>
#include <impl/ecs_csg/module.h>

#include <core/common/log.h>
#include <core/graphics/runtime/runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_resources{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static b8 ReserveCsgStructuredBuffer(const usize requiredCount, const usize elementByteSize, const Name& debugName, Core::GraphicsRuntime& graphics, Core::BufferHandle& buffer, usize& inOutCapacity){
    if(requiredCount == 0u)
        return true;
    if(buffer && inOutCapacity >= requiredCount)
        return true;
    NWB_ASSERT(elementByteSize != 0u);
    if(requiredCount > Limit<usize>::s_Max / elementByteSize)
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

    auto createdBufferResult = graphics.createBuffer(bufferDesc);
    if(!createdBufferResult)
        return false;
    Core::BufferHandle createdBuffer = Move(*createdBufferResult);

    buffer = Move(createdBuffer);
    inOutCapacity = capacity;
    return true;
}

[[nodiscard]] static Expected<Core::GpuDescriptorHandle> AcquireCsgBufferHeapHandle(const Core::GpuDescriptorClass::Enum descriptorClass, Core::Device& device, Core::Buffer& buffer){
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized())
        return MakeUnexpected(Failure{});

    NWB_ASSERT(descriptorClass == Core::GpuDescriptorClass::StorageBuffer || descriptorClass == Core::GpuDescriptorClass::UniformBuffer);
    const Core::DescriptorWriteItem descriptorWrite = descriptorClass == Core::GpuDescriptorClass::UniformBuffer ? Core::DescriptorWriteItem::ConstantBuffer(0u, &buffer) : Core::DescriptorWriteItem::StructuredBufferSrv(0u, &buffer);
    const Expected<Core::GpuDescriptorHandle> acquired = heap.allocate(descriptorClass);
    if(!acquired)
        return MakeUnexpected(acquired.error());
    if(!heap.write(*acquired, descriptorWrite)){
        heap.free(*acquired);
        return MakeUnexpected(Failure{});
    }

    return *acquired;
}

[[nodiscard]] static b8 ReplaceCsgStorageBufferHeapHandle(Core::Device& device, Core::Buffer& buffer, Core::GpuDescriptorHandle& inOutHandle){
    const auto acquired = AcquireCsgBufferHeapHandle(Core::GpuDescriptorClass::StorageBuffer, device, buffer);
    if(!acquired){
        // The buffer was replaced; drop the stale handle for a later retry.
        Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
        if(inOutHandle.valid() && heap.isInitialized())
            heap.free(inOutHandle);
        inOutHandle = Core::GpuDescriptorHandle::Invalid();
        return false;
    }

    // Replacements take a fresh slot; the old one retires after quarantine.
    if(inOutHandle.valid())
        device.getDescriptorHeap().free(inOutHandle);
    inOutHandle = *acquired;
    return true;
}

[[nodiscard]] static b8 EnsureCsgBufferHeapHandle(const Core::GpuDescriptorClass::Enum descriptorClass, Core::Device& device, Core::Buffer& buffer, Core::GpuDescriptorHandle& inOutHandle){
    if(inOutHandle.valid())
        return inOutHandle.descriptorClass() == descriptorClass;
    const auto acquired = AcquireCsgBufferHeapHandle(descriptorClass, device, buffer);
    if(!acquired)
        return false;
    inOutHandle = *acquired;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 CsgGraphResourceSnapshot::bindingValid()const noexcept{
    return
        receiverRanges
        && cutters
        && clipContextSlots
        && intervalSampleState
        && receiverRangeHeapHandle.valid()
        && receiverRangeHeapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer
        && cutterHeapHandle.valid()
        && cutterHeapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer
        && clipContextSlotsHeapHandle.valid()
        && clipContextSlotsHeapHandle.descriptorClass() == Core::GpuDescriptorClass::UniformBuffer
        && intervalSampleStateHeapHandle.valid()
        && intervalSampleStateHeapHandle.descriptorClass() == Core::GpuDescriptorClass::UniformBuffer
    ;
}

b8 CsgGraphResourceSnapshot::frameReady(const CsgFrameGpuDataView& csgFrameData)const noexcept{
    if(!csgFrameData.hasWork())
        return true;

    return bindingValid() && receiverRangeCapacity >= csgFrameData.receiverRanges.size() && cutterCapacity >= csgFrameData.cutters.size();
}

Expected<u32> CsgGraphResourceSnapshot::findClipContextHeapSlot()const noexcept{
    if(!clipContextSlotsHeapHandle.valid())
        return MakeUnexpected(Failure{});

    return clipContextSlotsHeapHandle.slot();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererCsgState::invalidateResources()noexcept{
    m_clipBindingLayout.reset();
    m_intervalPeelBindingLayout.reset();
    m_receiverSpanBuildBindingLayout.reset();
    m_intervalCombineBindingLayout.reset();
    m_intervalPeelComputeShader.reset();
    m_receiverSpanBuildComputeShader.reset();
    m_intervalCombineComputeShader.reset();
    m_intervalCapFillPixelShader.reset();
    m_refractionCapFillPixelShader.reset();
    m_intervalPeelPipeline.reset();
    m_receiverSpanBuildPipeline.reset();
    m_intervalCombinePipeline.reset();
    m_intervalCapFillPipeline.reset();
    m_refractionCapFillPipeline.reset();
    m_receiverRangeBuffer.reset();
    m_cutterBuffer.reset();
    m_clipContextSlotsBuffer.reset();
    m_receiverRangeBufferHeapHandle = Core::GpuDescriptorHandle::Invalid();
    m_cutterBufferHeapHandle = Core::GpuDescriptorHandle::Invalid();
    m_clipContextSlotsHeapHandle = Core::GpuDescriptorHandle::Invalid();
    m_intervalSampleStateBuffer.reset();
    m_intervalSampleStateHeapHandle = Core::GpuDescriptorHandle::Invalid();
    m_frameStateCacheSignature = CsgFrameStateCacheSignature{};
    m_frameStateCache = CsgFrameState{};
    m_receiverRangeBufferCapacity = 0u;
    m_cutterBufferCapacity = 0u;
    m_frameStateCacheValid = false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererCsgSystem::invalidateResources(){
    releaseCsgClipContextHeapHandles();
    m_csgState.invalidateResources();
}

b8 RendererCsgSystem::createCsgClipResources(){
    auto& device = m_graphics.getDevice();
    if(!m_csgState.m_clipBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc(m_arena);
        bindingLayoutDesc.setVisibility(Core::ShaderType::Mesh | Core::ShaderType::Compute | Core::ShaderType::Pixel);
        // Push-only layout for the cap-fill path.
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(ShaderDrivenPushConstants)));

        auto clipBindingLayoutResult = device.createBindingLayout(bindingLayoutDesc);
        if(clipBindingLayoutResult)
            m_csgState.m_clipBindingLayout = Move(*clipBindingLayoutResult);
        else
            m_csgState.m_clipBindingLayout.reset();
        if(!clipBindingLayoutResult){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG clip binding layout"));
            return false;
        }
    }

    if(!createCsgIntervalSampleStateBuffer())
        return false;
    if(!m_csgState.m_clipContextSlotsBuffer){
        Core::BufferDesc bufferDesc;
        bufferDesc.setByteSize(sizeof(CsgClipContextSlots)).setIsConstantBuffer(true).setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute).setDebugName("engine/csg/clip_context_slots").enableAutomaticStateTracking(Core::ResourceStates::Common);
        auto clipContextSlotsBufferResult = m_graphics.createBuffer(bufferDesc);
        if(clipContextSlotsBufferResult)
            m_csgState.m_clipContextSlotsBuffer = Move(*clipContextSlotsBufferResult);
        else
            m_csgState.m_clipContextSlotsBuffer.reset();
        if(!clipContextSlotsBufferResult){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG clip-context slot buffer"));
            return false;
        }
    }
    if(
        !__hidden_csg_resources::EnsureCsgBufferHeapHandle(Core::GpuDescriptorClass::UniformBuffer, device, *m_csgState.m_clipContextSlotsBuffer.get(), m_csgState.m_clipContextSlotsHeapHandle)
        || !__hidden_csg_resources::EnsureCsgBufferHeapHandle(Core::GpuDescriptorClass::UniformBuffer, device, *m_csgState.m_intervalSampleStateBuffer.get(), m_csgState.m_intervalSampleStateHeapHandle)
    )
        return false;

    return true;
}

b8 RendererCsgSystem::reserveCsgReceiverRangeBufferCapacity(const usize rangeCount){
    const usize oldCapacity = m_csgState.m_receiverRangeBufferCapacity;
    if(!__hidden_csg_resources::ReserveCsgStructuredBuffer(rangeCount, sizeof(CsgReceiverRangeGpuData), s_CsgReceiverRangeBufferName, m_graphics, m_csgState.m_receiverRangeBuffer, m_csgState.m_receiverRangeBufferCapacity)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG receiver range buffer"));
        return false;
    }

    if(m_csgState.m_receiverRangeBufferCapacity != oldCapacity && !__hidden_csg_resources::ReplaceCsgStorageBufferHeapHandle(m_graphics.getDevice(), *m_csgState.m_receiverRangeBuffer.get(), m_csgState.m_receiverRangeBufferHeapHandle)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register replacement CSG receiver range buffer in the descriptor heap"));
        return false;
    }
    return true;
}

b8 RendererCsgSystem::reserveCsgCutterBufferCapacity(const usize cutterCount){
    const usize oldCapacity = m_csgState.m_cutterBufferCapacity;
    if(!__hidden_csg_resources::ReserveCsgStructuredBuffer(cutterCount, sizeof(CsgCutterGpuData), s_CsgCutterBufferName, m_graphics, m_csgState.m_cutterBuffer, m_csgState.m_cutterBufferCapacity)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG cutter buffer"));
        return false;
    }

    if(m_csgState.m_cutterBufferCapacity != oldCapacity && !__hidden_csg_resources::ReplaceCsgStorageBufferHeapHandle(m_graphics.getDevice(), *m_csgState.m_cutterBuffer.get(), m_csgState.m_cutterBufferHeapHandle)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register replacement CSG cutter buffer in the descriptor heap"));
        return false;
    }
    return true;
}

b8 RendererCsgSystem::prepareCsgFrameResources(const usize receiverRangeCount, const usize cutterCount){
    if(receiverRangeCount == 0u || cutterCount == 0u)
        return true;
    NWB_ASSERT(receiverRangeCount <= static_cast<usize>(Limit<u32>::s_Max));
    NWB_ASSERT(cutterCount <= static_cast<usize>(Limit<u32>::s_Max));
    if(!reserveCsgReceiverRangeBufferCapacity(receiverRangeCount) || !reserveCsgCutterBufferCapacity(cutterCount))
        return false;
    if(
        !__hidden_csg_resources::EnsureCsgBufferHeapHandle(Core::GpuDescriptorClass::StorageBuffer, m_graphics.getDevice(), *m_csgState.m_receiverRangeBuffer.get(), m_csgState.m_receiverRangeBufferHeapHandle)
        || !__hidden_csg_resources::EnsureCsgBufferHeapHandle(Core::GpuDescriptorClass::StorageBuffer, m_graphics.getDevice(), *m_csgState.m_cutterBuffer.get(), m_csgState.m_cutterBufferHeapHandle)
        || !m_csgState.m_clipContextSlotsBuffer || !m_csgState.m_clipContextSlotsHeapHandle.valid()
        || !m_csgState.m_intervalSampleStateBuffer || !m_csgState.m_intervalSampleStateHeapHandle.valid()
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: CSG clip context heap registration is incomplete"));
        return false;
    }
    if(!m_csgState.m_clipBindingLayout)
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

ECSRenderDetail::CsgGraphResourceSnapshot RendererCsgSystem::csgGraphResourceSnapshot(const b8 retainRefractionCapPipeline)const noexcept{
    return {
        .receiverRanges = m_csgState.m_receiverRangeBuffer,
        .cutters = m_csgState.m_cutterBuffer,
        .clipContextSlots = m_csgState.m_clipContextSlotsBuffer,
        .intervalSampleState = m_csgState.m_intervalSampleStateBuffer,
        .refractionCapFillPipeline = retainRefractionCapPipeline ? m_csgState.m_refractionCapFillPipeline : nullptr,
        .receiverRangeCapacity = m_csgState.m_receiverRangeBufferCapacity,
        .cutterCapacity = m_csgState.m_cutterBufferCapacity,
        .receiverRangeHeapHandle = m_csgState.m_receiverRangeBufferHeapHandle,
        .cutterHeapHandle = m_csgState.m_cutterBufferHeapHandle,
        .clipContextSlotsHeapHandle = m_csgState.m_clipContextSlotsHeapHandle,
        .intervalSampleStateHeapHandle = m_csgState.m_intervalSampleStateHeapHandle,
    };
}

Expected<CsgClipContextSlots> RendererCsgSystem::prepareCsgClipContextSlotData(
    const DeferredFrameTargets& targets,
    const CsgFrameGpuDataView& csgFrameData,
    const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources,
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings
)const noexcept{
    CsgClipContextSlots contextSlots{};
    if(!csgFrameData.hasWork())
        return contextSlots;
    if(!csgResources.frameReady(csgFrameData) || !frameBindings.bindingValid() || !targets.bindless.slotsBufferDescriptor.valid())
        return MakeUnexpected(Failure{});

    // Freeze indirections now; later records must not see another generation.
    return ResolveCsgClipContextSlots(
        csgResources.receiverRangeHeapHandle.slot(),
        csgResources.cutterHeapHandle.slot(),
        frameBindings.materialTypedHeapHandle.slot(),
        frameBindings.instanceHeapHandle.slot(),
        targets.bindless.slotsBufferDescriptor.slot(),
        csgResources.intervalSampleStateHeapHandle.slot()
    );
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
    m_csgState.m_receiverRangeBufferHeapHandle = Core::GpuDescriptorHandle::Invalid();
    m_csgState.m_cutterBufferHeapHandle = Core::GpuDescriptorHandle::Invalid();
    m_csgState.m_clipContextSlotsHeapHandle = Core::GpuDescriptorHandle::Invalid();
    m_csgState.m_intervalSampleStateHeapHandle = Core::GpuDescriptorHandle::Invalid();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

