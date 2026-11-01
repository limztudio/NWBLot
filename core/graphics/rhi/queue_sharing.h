// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "command.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ResourceQueueSharing{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] constexpr Mask ForQueueClass(const CommandQueue::Enum queueClass)noexcept{
    switch(queueClass){
    case CommandQueue::Graphics:
        return Graphics;
    case CommandQueue::Compute:
        return AsyncCompute;
    case CommandQueue::Transfer:
        return Transfer;
    default:
        return Exclusive;
    }
}

[[nodiscard]] constexpr b8 IncludesQueueClass(const Mask sharing, const CommandQueue::Enum queueClass)noexcept{
    const Mask queueBit = ForQueueClass(queueClass);
    return queueBit != Exclusive && (sharing & queueBit) != Exclusive;
}

[[nodiscard]] constexpr b8 QueueFamilyIndexListContains(const Span<const u32> familyIndices, const u32 expectedFamilyIndex)noexcept{
    if(!familyIndices.data())
        return false;
    for(const u32 familyIndex : familyIndices){
        if(familyIndex == expectedFamilyIndex)
            return true;
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] constexpr b8 ResourceQueueAdmissionAdmitsQueue(const ResourceQueueAdmissionSnapshot& admission, const GpuPhysicalQueueInfo& queue)noexcept{
    if(!admission.valid())
        return false;
    if(!admission.usesConcurrentSharing)
        return true;
    if(!ResourceQueueSharing::IncludesQueueClass(admission.admittedQueueClasses, queue.queueClass))
        return false;

    return ResourceQueueSharing::QueueFamilyIndexListContains(Span<const u32>(admission.queueFamilyIndices, admission.queueFamilyIndexCount), queue.familyIndex);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

