// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "physical_queue_registrar.h"
#include "physical_queue_registration_policy.h"
#include "physical_queue_sharing_policy.h"

#include <core/common/log.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 Device::PhysicalQueueRegistrar::registerQueue(const VulkanPhysicalQueueDesc& desc, NativeQueueState& nativeQueue){
    const auto validation = PhysicalQueueRegistrationPolicy::Validate(desc, nativeQueue, m_device.m_physicalQueueInfos, m_device.m_explicitPrimaryQueues);
    switch(validation){
    case PhysicalQueueRegistrationPolicy::Result::Invalid:
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Refusing invalid physical queue registry entry."));
        return false;
    case PhysicalQueueRegistrationPolicy::Result::DuplicateQueue:
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Refusing duplicate physical queue family/index registry entry."));
        return false;
    case PhysicalQueueRegistrationPolicy::Result::DuplicatePrimary:
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Refusing duplicate primary physical queue class entry."));
        return false;
    case PhysicalQueueRegistrationPolicy::Result::Valid:
        break;
    }
    const u32 queueClassIndex = static_cast<u32>(desc.queueClass);

    const GpuPhysicalQueueInfo info{
        .familyIndex = nativeQueue.familyIndex,
        .queueIndex = nativeQueue.queueIndex,
        .timestampValidBits = desc.timestampValidBits,
        .id = GpuPhysicalQueueId{ .index = static_cast<u16>(m_device.m_physicalQueueInfos.size()), .deviceGeneration = m_device.m_deviceGeneration, },
        .queueClass = desc.queueClass,
        .capabilities = desc.capabilities,
        .dedicated = desc.dedicated,
    };
    Queue* const queue = NewArenaObject<Queue>(m_device.m_context.objectArena, m_device.m_context, m_device, info, nativeQueue);
    if(!queue)
        return false;
    ScopeExit destroyQueue([&]()noexcept{ DestroyArenaObjectNoexcept(m_device.m_context.objectArena, queue); });
    if(queue->m_trackingSemaphore == VK_NULL_HANDLE)
        return false;

    m_device.m_physicalQueueInfos.push_back(info);
    m_device.m_physicalQueues.push_back(MakeNotNull(queue));
    if(desc.primaryForClass){
        m_device.m_primaryQueues[queueClassIndex] = queue;
        m_device.m_explicitPrimaryQueues[queueClassIndex] = true;
    }
    else if(!m_device.m_primaryQueues[queueClassIndex])
        m_device.m_primaryQueues[queueClassIndex] = queue;
    destroyQueue.release();
    return true;
}

void Device::PhysicalQueueRegistrar::configureSharing()noexcept{
    const auto primaryFamily = [this](const CommandQueue::Enum queueClass)noexcept{
        const Queue* const queue = m_device.m_primaryQueues[static_cast<u32>(queueClass)];
        return queue ? queue->m_queueFamilyIndex : Limit<u32>::s_Max;
    };
    const auto sharing = PhysicalQueueSharingPolicy::Resolve(
        m_device.m_physicalQueueInfos,
        primaryFamily(CommandQueue::Graphics),
        primaryFamily(CommandQueue::Compute),
        primaryFamily(CommandQueue::Transfer)
    );
    m_device.m_context.graphicsQueueFamilyIndex = sharing.graphics;
    m_device.m_context.auxiliaryGraphicsQueueFamilyIndex = sharing.auxiliaryGraphics;
    m_device.m_context.asyncComputeQueueFamilyIndex = sharing.compute;
    m_device.m_context.auxiliaryAsyncComputeQueueFamilyIndex = sharing.auxiliaryCompute;
    m_device.m_context.transferQueueFamilyIndex = sharing.transfer;
    m_device.m_context.auxiliaryTransferQueueFamilyIndex = sharing.auxiliaryTransfer;
    m_device.m_context.asyncComputeLaneEnabled = sharing.asyncComputeLaneEnabled;
    m_device.m_context.transferQueueEnabled = sharing.transferQueueEnabled;
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

