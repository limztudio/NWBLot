// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_GRAPHICS_BACKEND_VULKAN) && NWB_GRAPHICS_BACKEND_VULKAN

#include <tests/common/test_context.h>

#include <gtest/gtest.h>

#include <core/graphics/vulkan/calibrated_timestamp_probe.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_calibrated_timestamp_probe_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static u32 s_CountCalls = 0u;
static u32 s_ListCalls = 0u;
static u32 s_TimestampCalls = 0u;
static b8 s_RetryFirstCount = false;
static b8 s_HasDeviceDomain = false;

VKAPI_ATTR VkResult VKAPI_CALL EnumerateDomains([[maybe_unused]] VkPhysicalDevice physicalDevice, u32* count, VkTimeDomainKHR* domains){
    if(!domains){
        ++s_CountCalls;
        *count = 1u;
        return s_RetryFirstCount && s_CountCalls == 1u ? VK_INCOMPLETE : VK_SUCCESS;
    }
    ++s_ListCalls;
    *count = 1u;
    domains[0] = s_HasDeviceDomain ? VK_TIME_DOMAIN_DEVICE_KHR : VK_TIME_DOMAIN_CLOCK_MONOTONIC_KHR;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL GetTimestamps([[maybe_unused]] VkDevice device, [[maybe_unused]] u32 count, [[maybe_unused]] const VkCalibratedTimestampInfoKHR* infos, u64* timestamps, u64* maxDeviation){
    ++s_TimestampCalls;
    *timestamps = 1u;
    *maxDeviation = 0u;
    return VK_SUCCESS;
}

TEST(CalibratedTimestampProbe, RetriesIncompleteEnumerationBeforeDeviceQuery){
    s_CountCalls = 0u;
    s_ListCalls = 0u;
    s_TimestampCalls = 0u;
    s_RetryFirstCount = true;
    s_HasDeviceDomain = true;
    Alloc::ScratchArena scratch(NWB::Tests::s_TestArena);
    EXPECT_TRUE(VulkanDetail::CalibratedTimestampProbe::query(VK_NULL_HANDLE, VK_NULL_HANDLE, &EnumerateDomains, &GetTimestamps, scratch));
    EXPECT_EQ(s_CountCalls, 2u);
    EXPECT_EQ(s_ListCalls, 1u);
    EXPECT_EQ(s_TimestampCalls, 1u);
}

TEST(CalibratedTimestampProbe, MissingDeviceDomainDoesNotQueryTimestamps){
    s_CountCalls = 0u;
    s_ListCalls = 0u;
    s_TimestampCalls = 0u;
    s_RetryFirstCount = false;
    s_HasDeviceDomain = false;
    Alloc::ScratchArena scratch(NWB::Tests::s_TestArena);
    EXPECT_FALSE(VulkanDetail::CalibratedTimestampProbe::query(VK_NULL_HANDLE, VK_NULL_HANDLE, &EnumerateDomains, &GetTimestamps, scratch));
    EXPECT_EQ(s_CountCalls, 1u);
    EXPECT_EQ(s_ListCalls, 1u);
    EXPECT_EQ(s_TimestampCalls, 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


