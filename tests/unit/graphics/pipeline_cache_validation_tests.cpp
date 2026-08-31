// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>

#include <core/graphics/vulkan/device_detail.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_pipeline_cache_validation_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core;
namespace Cache = Core::GraphicsBackend::VulkanDetail;
namespace CacheValidation = Cache::PipelineCacheDataValidation;

inline constexpr usize s_TestPipelineCacheSize = 48u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void WriteU32(Array<u8, s_TestPipelineCacheSize>& cacheData, const usize offset, const u32 value){
    cacheData[offset] = static_cast<u8>(value);
    cacheData[offset + 1u] = static_cast<u8>(value >> 8u);
    cacheData[offset + 2u] = static_cast<u8>(value >> 16u);
    cacheData[offset + 3u] = static_cast<u8>(value >> 24u);
}

[[nodiscard]] static VkPhysicalDeviceProperties MakePhysicalDeviceProperties(){
    VkPhysicalDeviceProperties properties{};
    properties.vendorID = 0x10deu;
    properties.deviceID = 0x2684u;
    for(usize uuidIndex = 0u; uuidIndex < VK_UUID_SIZE; ++uuidIndex)
        properties.pipelineCacheUUID[uuidIndex] = static_cast<u8>(uuidIndex + 1u);
    return properties;
}

[[nodiscard]] static Array<u8, s_TestPipelineCacheSize> MakePipelineCacheData(
    const VkPhysicalDeviceProperties& properties
){
    Array<u8, s_TestPipelineCacheSize> cacheData{};
    WriteU32(cacheData, 0u, static_cast<u32>(Cache::s_PipelineCacheHeaderVersionOneSize));
    WriteU32(cacheData, 4u, static_cast<u32>(VK_PIPELINE_CACHE_HEADER_VERSION_ONE));
    WriteU32(cacheData, 8u, properties.vendorID);
    WriteU32(cacheData, 12u, properties.deviceID);
    for(usize uuidIndex = 0u; uuidIndex < VK_UUID_SIZE; ++uuidIndex)
        cacheData[16u + uuidIndex] = properties.pipelineCacheUUID[uuidIndex];
    return cacheData;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(PipelineCacheValidation, AcceptsAnExactVersionOneHeaderWithOpaquePayload){
    const VkPhysicalDeviceProperties properties = MakePhysicalDeviceProperties();
    const Array<u8, s_TestPipelineCacheSize> cacheData = MakePipelineCacheData(properties);

    EXPECT_EQ(
        Cache::ValidatePipelineCacheData(BinaryByteView{ cacheData.data(), cacheData.size() }, properties),
        CacheValidation::Usable
    );
}

TEST(PipelineCacheValidation, RejectsStructurallyMalformedHeaders){
    const VkPhysicalDeviceProperties properties = MakePhysicalDeviceProperties();
    Array<u8, s_TestPipelineCacheSize> cacheData = MakePipelineCacheData(properties);

    EXPECT_EQ(Cache::ValidatePipelineCacheData(BinaryByteView{}, properties), CacheValidation::Malformed);
    EXPECT_EQ(
        Cache::ValidatePipelineCacheData(BinaryByteView{ nullptr, Cache::s_PipelineCacheHeaderVersionOneSize }, properties),
        CacheValidation::Malformed
    );
    EXPECT_EQ(
        Cache::ValidatePipelineCacheData(
            BinaryByteView{ cacheData.data(), Cache::s_PipelineCacheHeaderVersionOneSize - 1u },
            properties
        ),
        CacheValidation::Malformed
    );

    WriteU32(cacheData, 0u, 31u);
    EXPECT_EQ(
        Cache::ValidatePipelineCacheData(BinaryByteView{ cacheData.data(), cacheData.size() }, properties),
        CacheValidation::Malformed
    );

    WriteU32(cacheData, 0u, 33u);
    EXPECT_EQ(
        Cache::ValidatePipelineCacheData(BinaryByteView{ cacheData.data(), cacheData.size() }, properties),
        CacheValidation::Malformed
    );

    WriteU32(cacheData, 0u, static_cast<u32>(cacheData.size() + 1u));
    EXPECT_EQ(
        Cache::ValidatePipelineCacheData(BinaryByteView{ cacheData.data(), cacheData.size() }, properties),
        CacheValidation::Malformed
    );
}

TEST(PipelineCacheValidation, SeparatesUnsupportedAndDeviceIncompatibleData){
    const VkPhysicalDeviceProperties properties = MakePhysicalDeviceProperties();

    Array<u8, s_TestPipelineCacheSize> cacheData = MakePipelineCacheData(properties);
    WriteU32(cacheData, 4u, static_cast<u32>(VK_PIPELINE_CACHE_HEADER_VERSION_ONE) + 1u);
    EXPECT_EQ(
        Cache::ValidatePipelineCacheData(BinaryByteView{ cacheData.data(), cacheData.size() }, properties),
        CacheValidation::Incompatible
    );

    cacheData = MakePipelineCacheData(properties);
    WriteU32(cacheData, 8u, properties.vendorID + 1u);
    EXPECT_EQ(
        Cache::ValidatePipelineCacheData(BinaryByteView{ cacheData.data(), cacheData.size() }, properties),
        CacheValidation::Incompatible
    );

    cacheData = MakePipelineCacheData(properties);
    WriteU32(cacheData, 12u, properties.deviceID + 1u);
    EXPECT_EQ(
        Cache::ValidatePipelineCacheData(BinaryByteView{ cacheData.data(), cacheData.size() }, properties),
        CacheValidation::Incompatible
    );

    cacheData = MakePipelineCacheData(properties);
    cacheData[16u] ^= 0xffu;
    EXPECT_EQ(
        Cache::ValidatePipelineCacheData(BinaryByteView{ cacheData.data(), cacheData.size() }, properties),
        CacheValidation::Incompatible
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

