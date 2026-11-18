// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <engine_namespace.h>

#include <core/graphics/vulkan/backend_core_context.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::VulkanSdk{

using VulkanExtensions = decltype(Core::GraphicsBackend::VulkanContext::extensions);

[[nodiscard]] inline VkQueue MakeDummyVkQueue(const usize handle)noexcept{ return reinterpret_cast<VkQueue>(handle); }

template<class Receiver>
VKAPI_ATTR VkResult VKAPI_CALL QueryCooperativeVectorProperties(VkPhysicalDevice device, u32* count, VkCooperativeVectorPropertiesNV* properties){
    reinterpret_cast<Receiver*>(device)->query(*count, properties != nullptr);
    return VK_SUCCESS;
}

template<class Receiver>
void BindCooperativeVectorQuery(Core::GraphicsBackend::VulkanContext& context, Receiver& receiver)noexcept{
    context.physicalDevice = reinterpret_cast<VkPhysicalDevice>(&receiver);
    context.instanceDispatch.vkGetPhysicalDeviceCooperativeVectorPropertiesNV = &QueryCooperativeVectorProperties<Receiver>;
}

[[nodiscard]] inline b8 SharesQueuePriorities(const VkDeviceQueueCreateInfo& left, const VkDeviceQueueCreateInfo& right)noexcept{
    return left.pQueuePriorities == right.pQueuePriorities;
}

[[nodiscard]] inline f32 QueuePriorityAt(const VkDeviceQueueCreateInfo& queue, const usize index)noexcept{
    return queue.pQueuePriorities[index];
}

};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

