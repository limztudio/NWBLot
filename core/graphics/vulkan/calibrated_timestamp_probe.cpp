// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "calibrated_timestamp_probe.h"
#include "backend.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_calibrated_timestamp_probe{
    static constexpr u32 s_CalibratedTimestampProbeAttemptCount = 4u;
};


b8 CalibratedTimestampProbe::Query(const VkPhysicalDevice physicalDevice, const VkDevice device, const PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsKHR enumerateTimeDomains, const PFN_vkGetCalibratedTimestampsKHR getCalibratedTimestamps, Alloc::ScratchArena& scratchArena){
    if(!enumerateTimeDomains || !getCalibratedTimestamps){
        NWB_LOGGER_WARNING(FOUNDATION_TEXT("Vulkan: Calibrated timestamp entry points are unavailable."));
        return false;
    }

    for(u32 attempt = 0u; attempt < __hidden_calibrated_timestamp_probe::s_CalibratedTimestampProbeAttemptCount; ++attempt){
        u32 timeDomainCount = 0u;
        const VkResult countResult = enumerateTimeDomains(physicalDevice, &timeDomainCount, nullptr);
        if(countResult == VK_INCOMPLETE)
            continue;
        if(countResult != VK_SUCCESS){
            NWB_LOGGER_WARNING(FOUNDATION_TEXT("Vulkan: Failed to enumerate calibrated timestamp domains. {}"), ResultToString(countResult));
            return false;
        }
        if(timeDomainCount == 0u)
            return false;

        Vector<VkTimeDomainKHR, Alloc::ScratchArena> timeDomains(timeDomainCount, scratchArena);
        u32 writtenTimeDomainCount = timeDomainCount;
        const VkResult domainsResult = enumerateTimeDomains(physicalDevice, &writtenTimeDomainCount, timeDomains.data());
        if(domainsResult == VK_INCOMPLETE)
            continue;
        if(domainsResult != VK_SUCCESS){
            NWB_LOGGER_WARNING(FOUNDATION_TEXT("Vulkan: Failed to read calibrated timestamp domains. {}"), ResultToString(domainsResult));
            return false;
        }
        if(writtenTimeDomainCount > timeDomains.size()){
            NWB_LOGGER_WARNING(FOUNDATION_TEXT("Vulkan: Calibrated timestamp domain enumeration returned an invalid count."));
            return false;
        }

        b8 hasDeviceTimeDomain = false;
        for(u32 domainIndex = 0u; domainIndex < writtenTimeDomainCount; ++domainIndex){
            if(timeDomains[domainIndex] == VK_TIME_DOMAIN_DEVICE_KHR){
                hasDeviceTimeDomain = true;
                break;
            }
        }
        if(!hasDeviceTimeDomain)
            return false;

        VkCalibratedTimestampInfoKHR timestampInfo{};
        timestampInfo.sType = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_KHR;
        timestampInfo.timeDomain = VK_TIME_DOMAIN_DEVICE_KHR;
        u64 timestamp = 0u;
        u64 maxDeviation = 0u;
        const VkResult timestampResult = getCalibratedTimestamps(device, 1u, &timestampInfo, &timestamp, &maxDeviation);
        if(timestampResult != VK_SUCCESS){
            NWB_LOGGER_WARNING(FOUNDATION_TEXT("Vulkan: Failed to probe the calibrated device timestamp domain. {}"), ResultToString(timestampResult));
            return false;
        }
        return true;
    }

    NWB_LOGGER_WARNING(FOUNDATION_TEXT("Vulkan: Calibrated timestamp domain enumeration did not stabilize."));
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

