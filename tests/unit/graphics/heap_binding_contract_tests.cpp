// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>

#include <core/graphics/vulkan/heap_binding_contract.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_heap_binding_contract_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core;
namespace Binding = Core::GraphicsBackend::VulkanDetail;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(HeapBindingContract, BuildsCheckedAbsoluteRanges){
    Binding::HeapBindingRange range;
    EXPECT_TRUE(Binding::TryBuildHeapBindingRange(1024u, 256u, 256u, 128u, 128u, range));
    EXPECT_EQ(range.localOffset, 256u);
    EXPECT_EQ(range.size, 128u);
    EXPECT_EQ(range.absoluteBegin, 512u);
    EXPECT_EQ(range.absoluteEnd, 640u);

    EXPECT_FALSE(Binding::TryBuildHeapBindingRange(1024u, 0u, 0u, 0u, 1u, range));
    EXPECT_FALSE(Binding::TryBuildHeapBindingRange(1024u, 0u, 1025u, 1u, 1u, range));
    EXPECT_FALSE(Binding::TryBuildHeapBindingRange(1024u, 0u, 960u, 65u, 1u, range));
    EXPECT_FALSE(Binding::TryBuildHeapBindingRange(1024u, 1u, 0u, 64u, 64u, range));
    EXPECT_FALSE(Binding::TryBuildHeapBindingRange(
        Limit<u64>::s_Max,
        Limit<u64>::s_Max,
        1u,
        1u,
        1u,
        range
    ));
}

TEST(HeapBindingContract, RejectsRawOverlapAndCrossClassGranularityPages){
    Binding::HeapBindingRange bufferRange;
    Binding::HeapBindingRange samePageImageRange;
    Binding::HeapBindingRange nextPageImageRange;
    Binding::HeapBindingRange touchingBufferRange;
    ASSERT_TRUE(Binding::TryBuildHeapBindingRange(1024u, 0u, 0u, 64u, 1u, bufferRange));
    ASSERT_TRUE(Binding::TryBuildHeapBindingRange(1024u, 0u, 64u, 64u, 1u, samePageImageRange));
    ASSERT_TRUE(Binding::TryBuildHeapBindingRange(1024u, 0u, 256u, 64u, 1u, nextPageImageRange));
    ASSERT_TRUE(Binding::TryBuildHeapBindingRange(1024u, 0u, 64u, 64u, 1u, touchingBufferRange));

    EXPECT_TRUE(Binding::HeapBindingRangesConflict(
        bufferRange,
        Binding::HeapBindingResourceClass::Buffer,
        bufferRange,
        Binding::HeapBindingResourceClass::Buffer,
        256u
    ));
    EXPECT_FALSE(Binding::HeapBindingRangesConflict(
        bufferRange,
        Binding::HeapBindingResourceClass::Buffer,
        touchingBufferRange,
        Binding::HeapBindingResourceClass::Buffer,
        256u
    ));
    EXPECT_TRUE(Binding::HeapBindingRangesConflict(
        bufferRange,
        Binding::HeapBindingResourceClass::Buffer,
        samePageImageRange,
        Binding::HeapBindingResourceClass::OptimalImage,
        256u
    ));
    EXPECT_TRUE(Binding::HeapBindingRangesConflict(
        samePageImageRange,
        Binding::HeapBindingResourceClass::OptimalImage,
        bufferRange,
        Binding::HeapBindingResourceClass::Buffer,
        256u
    ));
    EXPECT_FALSE(Binding::HeapBindingRangesConflict(
        bufferRange,
        Binding::HeapBindingResourceClass::Buffer,
        nextPageImageRange,
        Binding::HeapBindingResourceClass::OptimalImage,
        256u
    ));
    EXPECT_FALSE(Binding::HeapBindingRangesConflict(
        nextPageImageRange,
        Binding::HeapBindingResourceClass::OptimalImage,
        bufferRange,
        Binding::HeapBindingResourceClass::Buffer,
        256u
    ));
}

TEST(HeapBindingContract, FiltersProtectedAndIncompatibleMemoryTypes){
    VkPhysicalDeviceMemoryProperties properties{};
    properties.memoryTypeCount = 3u;
    properties.memoryTypes[0u].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    properties.memoryTypes[1u].propertyFlags = VK_MEMORY_PROPERTY_PROTECTED_BIT;
    properties.memoryTypes[2u].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;

    EXPECT_EQ(Binding::BuildNonProtectedMemoryTypeBits(properties), 0x5u);
    EXPECT_TRUE(Binding::IsHeapMemoryTypeCompatible(properties, 0u, 0x3u));
    EXPECT_FALSE(Binding::IsHeapMemoryTypeCompatible(properties, 1u, 0x3u));
    EXPECT_FALSE(Binding::IsHeapMemoryTypeCompatible(properties, 2u, 0x3u));
    EXPECT_FALSE(Binding::IsHeapMemoryTypeCompatible(properties, 3u, UINT32_MAX));

    VkMemoryDedicatedRequirements dedicatedRequirements{};
    dedicatedRequirements.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS;
    EXPECT_TRUE(Binding::AllowsGenericHeapBinding(dedicatedRequirements));
    dedicatedRequirements.requiresDedicatedAllocation = VK_TRUE;
    EXPECT_FALSE(Binding::AllowsGenericHeapBinding(dedicatedRequirements));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

