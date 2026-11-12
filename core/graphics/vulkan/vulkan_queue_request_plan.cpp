// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_queue_request_plan.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


VulkanQueueRequestPlan::VulkanQueueRequestPlan(Alloc::ScratchArena& scratchArena, const Input& input)
    : m_sameClassQueueRequests(scratchArena)
    , m_uniqueQueueFamilies(scratchArena)
    , m_queuePriorities(scratchArena)
    , m_queueDescriptions(scratchArena)
{
    const auto& physicalQueueFamilies = input.physicalQueueFamilies;
    auto& sameClassQueueRequests = m_sameClassQueueRequests;
    const auto appendSameClassQueue = [&sameClassQueueRequests](const CommandQueue::Enum queueClass, const i32 family, const u32 queueIndex){
        for(const SameClassQueueRequest& existing : sameClassQueueRequests){
            if(existing.family == family && existing.queueIndex == queueIndex){
                FOUNDATION_ASSERT(existing.queueClass == queueClass);
                return;
            }
        }
        sameClassQueueRequests.push_back(SameClassQueueRequest{ .queueClass = queueClass, .family = family, .queueIndex = queueIndex, });
    };
    const auto appendPrimaryFamilyQueues = [&appendSameClassQueue, &physicalQueueFamilies, &input](const CommandQueue::Enum queueClass, const i32 primaryFamily, const u32 firstAdditionalQueueIndex){
        if(!input.deviceParams.enableSameClassMultiQueue || primaryFamily == s_InvalidQueueFamilyIndex || static_cast<usize>(primaryFamily) >= physicalQueueFamilies.size())
            return;

        const u32 queueCount = physicalQueueFamilies[static_cast<usize>(primaryFamily)].queueCount;
        for(u32 queueIndex = firstAdditionalQueueIndex; queueIndex < queueCount; ++queueIndex)
            appendSameClassQueue(queueClass, primaryFamily, queueIndex);
    };

    // Cross-family auxiliary stays the first non-primary entry; primary-family extras register after it.
    if(input.createCrossFamilySecondaryGraphicsQueue)
        appendSameClassQueue(CommandQueue::Graphics, input.secondaryGraphicsQueueFamily, s_GraphicsQueueIndex);
    appendPrimaryFamilyQueues(CommandQueue::Graphics, input.graphicsQueueFamily, s_SecondaryGraphicsQueueIndex);

    if(input.createCrossFamilySecondaryComputeQueue)
        appendSameClassQueue(CommandQueue::Compute, input.secondaryComputeQueueFamily, s_ComputeQueueIndex);
    if(input.createComputeQueue){
        appendPrimaryFamilyQueues(CommandQueue::Compute, input.schedulerComputeQueueFamily, s_ComputeQueueIndex + 1u);
    }

    if(input.createCrossFamilySecondaryTransferQueue)
        appendSameClassQueue(CommandQueue::Transfer, input.secondaryTransferQueueFamily, s_TransferQueueIndex);
    if(input.createDedicatedTransferQueue){
        appendPrimaryFamilyQueues(CommandQueue::Transfer, input.transferQueueFamily, s_TransferQueueIndex + 1u);
    }

    auto& uniqueQueueFamilies = m_uniqueQueueFamilies;
    const auto appendUniqueQueueFamily = [&uniqueQueueFamilies](const i32 queueFamily){
        if(queueFamily == s_InvalidQueueFamilyIndex)
            return;
        for(const i32 existingQueueFamily : uniqueQueueFamilies){
            if(existingQueueFamily == queueFamily)
                return;
        }
        uniqueQueueFamilies.push_back(queueFamily);
    };
    appendUniqueQueueFamily(input.graphicsQueueFamily);
    for(const SameClassQueueRequest& request : sameClassQueueRequests)
        appendUniqueQueueFamily(request.family);

    if(!input.deviceParams.headlessDevice)
        appendUniqueQueueFamily(input.presentQueueFamily);
    if(input.createComputeQueue)
        appendUniqueQueueFamily(input.schedulerComputeQueueFamily);
    if(input.createCrossFamilySecondaryComputeQueue)
        appendUniqueQueueFamily(input.secondaryComputeQueueFamily);
    if(input.createDedicatedTransferQueue)
        appendUniqueQueueFamily(input.transferQueueFamily);
    if(input.createCrossFamilySecondaryTransferQueue)
        appendUniqueQueueFamily(input.secondaryTransferQueueFamily);

    u32 maxQueueCount = 1u;
    const auto queueCountForFamily = [&sameClassQueueRequests](const i32 queueFamily)noexcept{
        u32 queueCount = 1u;
        for(const SameClassQueueRequest& request : sameClassQueueRequests){
            if(request.family == queueFamily && request.queueIndex >= queueCount)
                queueCount = request.queueIndex + 1u;
        }
        return queueCount;
    };
    for(const i32 queueFamily : uniqueQueueFamilies){
        const u32 queueCount = queueCountForFamily(queueFamily);
        if(queueCount > maxQueueCount)
            maxQueueCount = queueCount;
    }
    m_queuePriorities.resize(maxQueueCount);
    auto& queuePriorities = m_queuePriorities;
    for(f32& priority : queuePriorities)
        priority = 1.f;
    m_queueDescriptions.resize(uniqueQueueFamilies.size());
    auto& queueDesc = m_queueDescriptions;
    usize queueIndex = 0u;
    for(i32 queueFamily : uniqueQueueFamilies){
        VkDeviceQueueCreateInfo queueInfo = {};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = static_cast<u32>(queueFamily);
        queueInfo.queueCount = queueCountForFamily(queueFamily);
        queueInfo.pQueuePriorities = queuePriorities.data();
        queueDesc[queueIndex] = queueInfo;
        ++queueIndex;
    }

}


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

