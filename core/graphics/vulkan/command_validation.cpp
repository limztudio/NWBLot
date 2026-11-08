// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "texture_image_policy.h"
#include "command_validation.h"
#include "vulkan_viewport_policy.h"
#include "backend.h"
#include "buffer_resource_detail.h"
#include "texture_resource_detail.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



b8 IsPipelineColorAttachmentFormatClassValid(const Format::Enum format)noexcept{
    if(ConvertFormat(format) == VK_FORMAT_UNDEFINED)
        return false;
    const FormatInfo& formatInfo = GetFormatInfo(format);
    return !formatInfo.hasDepth && !formatInfo.hasStencil;
}

MeshDispatchLimits GetMeshDispatchLimits(const VkPhysicalDeviceMeshShaderPropertiesEXT& properties, const b8 taskShaderActive)noexcept{
    return taskShaderActive
        ? MeshDispatchLimits{ { properties.maxTaskWorkGroupCount[0u], properties.maxTaskWorkGroupCount[1u], properties.maxTaskWorkGroupCount[2u] }, properties.maxTaskWorkGroupTotalCount }
        : MeshDispatchLimits{ { properties.maxMeshWorkGroupCount[0u], properties.maxMeshWorkGroupCount[1u], properties.maxMeshWorkGroupCount[2u] }, properties.maxMeshWorkGroupTotalCount }
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 CommandList::validateViewportState(const ViewportState& viewportState, const TStringView operationName){
    if(viewportState.viewports.size() > 1u || viewportState.scissorRects.size() > 1u){
        rejectCommandRecording(operationName, NWB_TEXT("only one viewport and scissor are supported"));
        return false;
    }
    if(!viewportState.viewports.empty() && !VulkanViewportPolicy::isViewportValid(viewportState.viewports[0u], m_context.physicalDeviceProperties.limits)){
        rejectCommandRecording(operationName, NWB_TEXT("viewport is outside Vulkan device limits"));
        return false;
    }
    if(!viewportState.scissorRects.empty() && !viewportState.scissorRects[0u].validScissor()){
        rejectCommandRecording(operationName, NWB_TEXT("scissor rectangle is invalid"));
        return false;
    }
    if(viewportState.scissorRects.empty() && !viewportState.viewports.empty() && !VulkanViewportPolicy::isImplicitScissorValid(viewportState.viewports[0u])){
        rejectCommandRecording(operationName, NWB_TEXT("viewport cannot be converted to an implicit scissor"));
        return false;
    }
    return true;
}

b8 CommandList::validateTextureForGpuState(Texture* const texture, const ResourceStates::Mask requiredState, const TStringView operationName, const VkImageUsageFlags explicitRequiredUsage){
    if(!publicCommandStateAccessible())
        return false;
    if(!VulkanTextureDetail::IsTextureResourceStateMaskValid(requiredState)){
        rejectCommandRecording(operationName, NWB_TEXT("state is invalid for a texture"));
        return false;
    }
    const VkImageUsageFlags requiredUsage = explicitRequiredUsage | VulkanTextureDetail::TextureImagePolicy::requiredImageUsageForResourceStates(requiredState);
    if(!isTextureReadyForCommandQueue(texture, requiredUsage)){
        rejectCommandRecording(operationName, s_TextureNotReadyForQueueReason);
        return false;
    }

    const ResourceStates::Mask permanentState = m_stateTracker.getPermanentTextureState(texture);
    if(permanentState != ResourceStates::Unknown && permanentState != requiredState){
        rejectCommandRecording(operationName, NWB_TEXT("state conflicts with the permanent texture state"));
        return false;
    }
    return true;
}

b8 CommandList::validateBufferForGpuState(Buffer* const buffer, const ResourceStates::Mask requiredState, const TStringView operationName, const VkBufferUsageFlags explicitRequiredUsage){
    if(!publicCommandStateAccessible())
        return false;
    if(!VulkanBufferDetail::IsBufferResourceStateMaskValid(requiredState)){
        rejectCommandRecording(operationName, NWB_TEXT("state is invalid for a buffer"));
        return false;
    }
    if(buffer && !VulkanBufferDetail::IsBufferDescriptionCompatibleWithResourceStates(buffer->m_creationDesc, requiredState)){
        rejectCommandRecording(operationName, NWB_TEXT("state requires an undeclared buffer capability"));
        return false;
    }
    const VkBufferUsageFlags requiredUsage = explicitRequiredUsage | (buffer ? VulkanBufferDetail::RequiredBufferUsageForResourceStates(buffer->m_creationDesc, requiredState) : 0u);
    if(!isBufferReadyForCommandQueue(buffer, requiredUsage)){
        rejectCommandRecording(operationName, s_BufferNotReadyForQueueReason);
        return false;
    }

    const ResourceStates::Mask permanentState = m_stateTracker.getPermanentBufferState(buffer);
    if(permanentState != ResourceStates::Unknown && permanentState != requiredState){
        rejectCommandRecording(operationName, NWB_TEXT("state conflicts with the permanent buffer state"));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

