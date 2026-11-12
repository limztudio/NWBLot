// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "backend.h"
#include <core/alloc/scratch.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


class VulkanQueueRequestPlan final : NoCopy{
public:
    struct SameClassQueueRequest{
        CommandQueue::Enum queueClass = CommandQueue::kCount;
        i32 family = s_InvalidQueueFamilyIndex;
        u32 queueIndex = 0u;
    };

    struct Input{
        const DeviceCreationParameters& deviceParams;
        const Vector<VkQueueFamilyProperties, Alloc::ScratchArena>& physicalQueueFamilies;
        i32 graphicsQueueFamily;
        i32 secondaryGraphicsQueueFamily;
        i32 presentQueueFamily;
        i32 schedulerComputeQueueFamily;
        i32 secondaryComputeQueueFamily;
        i32 transferQueueFamily;
        i32 secondaryTransferQueueFamily;
        b8 createCrossFamilySecondaryGraphicsQueue;
        b8 createComputeQueue;
        b8 createCrossFamilySecondaryComputeQueue;
        b8 createDedicatedTransferQueue;
        b8 createCrossFamilySecondaryTransferQueue;
    };

    VulkanQueueRequestPlan(Alloc::ScratchArena& scratchArena, const Input& input);
    [[nodiscard]] const Vector<SameClassQueueRequest, Alloc::ScratchArena>& sameClassQueueRequests()const noexcept{ return m_sameClassQueueRequests; }
    [[nodiscard]] const Vector<VkDeviceQueueCreateInfo, Alloc::ScratchArena>& queueDescriptions()const noexcept{ return m_queueDescriptions; }

private:
    Vector<SameClassQueueRequest, Alloc::ScratchArena> m_sameClassQueueRequests;
    Vector<i32, Alloc::ScratchArena> m_uniqueQueueFamilies;
    Vector<f32, Alloc::ScratchArena> m_queuePriorities;
    Vector<VkDeviceQueueCreateInfo, Alloc::ScratchArena> m_queueDescriptions;
};


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

