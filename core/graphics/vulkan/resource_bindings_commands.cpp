// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::bindDescriptorBufferHeap(
    GpuDescriptorHeap& heap,
    const VkPipelineBindPoint bindPoint,
    const VkPipelineLayout pipelineLayout,
    const GpuDescriptorHandle accelStructHandle
){
    // Bind global resource/sampler sets and optional immutable TLAS generation at 8/9/10.
    if(!m_currentCmdBuf || pipelineLayout == VK_NULL_HANDLE)
        return;
    if(!heap.isInitialized())
        return;
    if(!m_context.descriptorBufferManager || !m_context.descriptorBufferManager->isEnabled())
        return;

    // Register before reading heap blocks so a concurrent free cannot recycle a descriptor while this command buffer
    // is about to bind it. The accepted queue submission later converts this recording use into a timeline token.
    heap.trackCommandBufferUse(*m_currentCmdBuf);

    const DescriptorBufferSegment& resourceBlock = heap.getResourceBufferBlock();
    const DescriptorBufferSegment& samplerBlock = heap.getSamplerBufferBlock();
    const DescriptorBufferSegment accelStructBlock = heap.getAccelStructBufferBlock(accelStructHandle);
    const bool bindAccelStruct = accelStructHandle.valid();
    if(
        !resourceBlock.valid()
        || resourceBlock.kind != DescriptorBufferSegmentKind::Resource
        || !samplerBlock.valid()
        || samplerBlock.kind != DescriptorBufferSegmentKind::Sampler
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Cannot bind descriptor-buffer heap: persistent resource or sampler block is invalid."));
        return;
    }
    if(bindAccelStruct && (!accelStructBlock.valid() || accelStructBlock.kind != DescriptorBufferSegmentKind::Resource)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Cannot bind descriptor-buffer TLAS heap handle {}: descriptor block is invalid."), accelStructHandle.value);
        return;
    }

    const u32 offsetAlignmentBytes = m_context.descriptorBufferManager->getOffsetAlignmentBytes();
    if(
        (resourceBlock.offsetBytes % offsetAlignmentBytes) != 0u
        || (samplerBlock.offsetBytes % offsetAlignmentBytes) != 0u
        || (bindAccelStruct && (accelStructBlock.offsetBytes % offsetAlignmentBytes) != 0u)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Cannot bind descriptor-buffer heap: a set block offset is misaligned."));
        return;
    }

    ensureDescriptorBuffersBound();

    // Resource, sampler, and TLAS heap sets are contiguous at 8/9/10.
    const u32 resourceIndex = m_context.descriptorBufferManager->getResourceBufferIndex();
    const u32 samplerIndex = m_context.descriptorBufferManager->getSamplerBufferIndex();
    const u32 bufferIndices[DescriptorBufferManager::s_DescriptorBufferCountWithAccelStruct] = { resourceIndex, samplerIndex, resourceIndex };
    const VkDeviceSize offsets[DescriptorBufferManager::s_DescriptorBufferCountWithAccelStruct] = {
        resourceBlock.offsetBytes,
        samplerBlock.offsetBytes,
        accelStructBlock.offsetBytes
    };

    vkCmdSetDescriptorBufferOffsetsEXT(
        m_currentCmdBuf->m_cmdBuf,
        bindPoint,
        pipelineLayout,
        heap.getResourceSetIndex(),
        bindAccelStruct
            ? DescriptorBufferManager::s_DescriptorBufferCountWithAccelStruct
            : DescriptorBufferManager::s_PersistentDescriptorBufferCount,
        bufferIndices,
        offsets
    );
}

void CommandList::ensureDescriptorBuffersBound(){
    if(m_descriptorBuffersBound)
        return;

    const DescriptorBufferManager& manager = *m_context.descriptorBufferManager;
    const VkDescriptorBufferBindingInfoEXT bindingInfos[DescriptorBufferManager::s_PersistentDescriptorBufferCount] = {
        manager.getResourceBindingInfo(),
        manager.getSamplerBindingInfo()
    };
    vkCmdBindDescriptorBuffersEXT(
        m_currentCmdBuf->m_cmdBuf,
        DescriptorBufferManager::s_PersistentDescriptorBufferCount,
        bindingInfos
    );
    m_descriptorBuffersBound = true;
}

void CommandList::bindDescriptorBufferEmptySet(const VkPipelineBindPoint bindPoint, const VkPipelineLayout pipelineLayout){
    if(!m_currentCmdBuf || pipelineLayout == VK_NULL_HANDLE)
        return;
    if(!m_context.descriptorBufferManager || !m_context.descriptorBufferManager->isEnabled())
        return;

    // Select empty set 0 to establish descriptor-buffer state for push-only layouts.
    ensureDescriptorBuffersBound();

    const u32 resourceBufferIndex = m_context.descriptorBufferManager->getResourceBufferIndex();
    constexpr VkDeviceSize s_EmptySetOffsetBytes = 0u;
    vkCmdSetDescriptorBufferOffsetsEXT(
        m_currentCmdBuf->m_cmdBuf,
        bindPoint,
        pipelineLayout,
        0u,
        1u,
        &resourceBufferIndex,
        &s_EmptySetOffsetBytes
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

