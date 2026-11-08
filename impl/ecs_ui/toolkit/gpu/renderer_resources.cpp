// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_internal.h"
#include "image_resources.h"
#include "target_factory.h"
#include "shared_resources_factory.h"

#include <core/common/log.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_gpu_resources{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename TElement>
[[nodiscard]] static b8 EnsureGeometryBuffer(
    Core::Device& device,
    const usize requiredCount,
    const usize initialCapacity,
    const Name& debugName,
    const b8 isVertexBuffer,
    Core::BufferHandle& inOutBuffer,
    usize& inOutCapacity
){
    if(inOutCapacity >= requiredCount)
        return true;
    const usize capacity = ComputeGpuGrowingBufferCapacity(inOutCapacity, requiredCount, initialCapacity, sizeof(TElement));
    Core::BufferDesc description;
    description.setByteSize(static_cast<u64>(capacity * sizeof(TElement))).setIsVertexBuffer(isVertexBuffer).setIsIndexBuffer(!isVertexBuffer)
        .setDebugName(debugName).enableAutomaticStateTracking(Core::ResourceStates::Common)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer)
    ;
    auto bufferResult = device.createBuffer(description);
    if(!bufferResult)
        return false;
    inOutBuffer = Move(*bufferResult);
    inOutCapacity = capacity;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void FreeGpuDescriptor(Core::GraphicsRuntime& graphics, const Core::GpuDescriptorHandle& descriptor)noexcept{
    if(descriptor.valid())
        graphics.getDevice().getDescriptorHeap().free(descriptor);
}

b8 GpuTokenComplete(Core::Device& device, const Core::QueueSubmissionToken& token){
    if(!token.valid())
        return true;
    if(!token.hasPhysicalQueueIdentity())
        return false;
    const Core::GpuPhysicalQueueId queue{ token.physicalQueueIndex, token.deviceGeneration };
    const auto timeline = device.getQueueTimelineSnapshot(queue);
    return timeline && timeline->valid() && timeline->queue == queue && timeline->completedValue >= token.value;
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 GpuRendererState::MatchesAcquired(const Core::AcquiredPresentationFrame& first, const Core::AcquiredPresentationFrame& second)noexcept{
    return
        first.valid() && second.valid()
        && first.backBuffer.texture == second.backBuffer.texture && first.framebuffer == second.framebuffer
        && first.backBuffer.index == second.backBuffer.index && first.backBuffer.nativeInitialState == second.backBuffer.nativeInitialState
        && first.backBuffer.availabilityCompletion == second.backBuffer.availabilityCompletion
    ;
}

b8 GpuRendererState::createResources(){
    return GpuSharedResourcesFactory{}.create(*this);
}

Expected<GpuVersion<GpuTargetVersion>> GpuRendererState::createTarget(const u32 width, const u32 height){
    return GpuTargetFactory{}.create(m_arena, m_graphics, width, height);
}

b8 GpuRendererState::prepareBuffers(GpuFrameSlot& slot, const DrawSnapshot& snapshot){
    const usize vertexCount = snapshot.vertices().size();
    const usize indexCount = snapshot.indices().size();
    if(vertexCount == 0u && indexCount == 0u)
        return true;
    if(!IsValidGpuSnapshotBufferCounts(vertexCount, indexCount, sizeof(Vertex), sizeof(u32)))
        return false;
    Core::Device& device = m_graphics.getDevice();
    return __hidden_ui_gpu_resources::EnsureGeometryBuffer<Vertex>(device, vertexCount, s_GpuInitialVertexCapacity, s_GpuVertexBufferName, true, slot.vertices, slot.vertexCapacity)
        && __hidden_ui_gpu_resources::EnsureGeometryBuffer<u32>(device, indexCount, s_GpuInitialIndexCapacity, s_GpuIndexBufferName, false, slot.indices, slot.indexCapacity);
}

b8 GpuRendererState::prepareOutputPipeline(const Core::FramebufferInfo& info){
    return GpuSharedResourcesFactory{}.prepareOutputPipeline(*this, info);
}

b8 GpuRendererState::prepare(const Core::AcquiredPresentationFrame& acquired){
    m_readyToDeclare = false;
    if(!m_pending)
        return true;
    if(!acquired.valid() || !m_resources || m_width == 0u || m_height == 0u)
        return false;
    const Core::FramebufferDesc& attachments = acquired.framebuffer->getDescription();
    const Core::TextureDesc& backBuffer = acquired.backBuffer.texture->getDescription();
    if(
        attachments.colorAttachments.size() != 1u || attachments.colorAttachments[0].texture != acquired.backBuffer.texture.get()
        || attachments.depthAttachment.valid() || attachments.shadingRateAttachment.valid()
        || backBuffer.width != m_width || backBuffer.height != m_height || backBuffer.sampleCount != 1u
    )
        return false;
    if(!m_resources->m_outputPipeline || m_resources->m_outputPipeline->getFramebufferInfo() != acquired.framebuffer->getFramebufferInfo())
        return false;
    if(m_pending->m_prepared && MatchesAcquired(m_pending->m_acquired, acquired)){
        m_readyToDeclare = true;
        return true;
    }
    Core::Device& device = m_graphics.getDevice();
    if(m_pending->m_prepared){
        // A failed graph may have accepted uploads or raster. Rebuild only after that exact prefix completes.
        if(!m_pending->prefixComplete(device))
            return true;
        m_pending->m_acquired = acquired;
        m_readyToDeclare = true;
        m_claimed = false;
        m_declaredGraph = nullptr;
        return true;
    }
    // Retire all completed owners before cache eviction or descriptor allocation can need their heap slots.
    for(auto& slot : m_slots){
        if(slot.inFlight && slot.inFlight->complete(device))
            slot.inFlight.reset();
    }
    m_imageCache.trim(m_pending->m_snapshot);
    for(auto& slot : m_slots){
        if(slot.inFlight)
            continue;
        GpuImageResources imageResources(m_arena, m_graphics, m_imageCache);
        if(!imageResources.prepareGlyphPages(*m_pending) || !imageResources.prepareSdfPages(*m_pending) || !imageResources.prepareTextureImages(*m_pending) || !prepareBuffers(slot, m_pending->m_snapshot))
            return false;
        m_pending->m_target = slot.target;
        m_pending->m_resources = m_resources;
        m_pending->m_vertices = slot.vertices;
        m_pending->m_indices = slot.indices;
        m_pending->m_outputPipeline = m_resources->m_outputPipeline;
        m_pending->m_acquired = acquired;
        m_pending->m_presentationMode = m_graphics.isHDR10OutputActive() ? NWB_UI_PRESENTATION_HDR10 : NWB_UI_PRESENTATION_SDR;
        m_pending->m_prepared = true;
        m_readyToDeclare = true;
        slot.inFlight = m_pending;
        m_claimed = false;
        m_declaredGraph = nullptr;
        return true;
    }
    // Admission is bounded. The pending immutable CPU generation survives until a slot becomes available.
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

