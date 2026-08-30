// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>

#include <core/graphics/vulkan/heap_binding_contract.h>
#include <core/graphics/vulkan/resource_bindings_detail.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_heap_binding_contract_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core;
namespace Binding = Core::GraphicsBackend::VulkanDetail;
using DescriptorBufferStartupPrerequisites = Binding::DescriptorBufferStartupPrerequisites;
using DescriptorPrerequisiteMember = bool DescriptorBufferStartupPrerequisites::*;


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

TEST(DescriptorBufferStartupPrerequisites, RequiresBdaAndEveryRuntimeEntryPoint){
    const DescriptorBufferStartupPrerequisites prerequisites{
        .descriptorBufferExtensionEnabled = true,
        .bufferDeviceAddressFeatureEnabled = true,
        .getBufferDeviceAddressAvailable = true,
        .getDescriptorAvailable = true,
        .getDescriptorSetLayoutSizeAvailable = true,
        .getDescriptorSetLayoutBindingOffsetAvailable = true,
        .cmdBindDescriptorBuffersAvailable = true,
        .cmdSetDescriptorBufferOffsetsAvailable = true,
    };
    EXPECT_TRUE(Binding::HasDescriptorBufferStartupPrerequisites(prerequisites));

    static constexpr DescriptorPrerequisiteMember s_PrerequisiteMembers[] = {
        &DescriptorBufferStartupPrerequisites::descriptorBufferExtensionEnabled,
        &DescriptorBufferStartupPrerequisites::bufferDeviceAddressFeatureEnabled,
        &DescriptorBufferStartupPrerequisites::getBufferDeviceAddressAvailable,
        &DescriptorBufferStartupPrerequisites::getDescriptorAvailable,
        &DescriptorBufferStartupPrerequisites::getDescriptorSetLayoutSizeAvailable,
        &DescriptorBufferStartupPrerequisites::getDescriptorSetLayoutBindingOffsetAvailable,
        &DescriptorBufferStartupPrerequisites::cmdBindDescriptorBuffersAvailable,
        &DescriptorBufferStartupPrerequisites::cmdSetDescriptorBufferOffsetsAvailable,
    };
    for(usize prerequisiteIndex = 0u; prerequisiteIndex < LengthOf(s_PrerequisiteMembers); ++prerequisiteIndex){
        DescriptorBufferStartupPrerequisites missingPrerequisite = prerequisites;
        missingPrerequisite.*s_PrerequisiteMembers[prerequisiteIndex] = false;
        EXPECT_FALSE(Binding::HasDescriptorBufferStartupPrerequisites(missingPrerequisite)) << prerequisiteIndex;
    }
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

TEST(HeapBindingContract, ResolvesBufferCpuAccessAndMatchesHeapTypes){
    struct CpuAccessCase{
        CpuAccessMode::Enum declaredAccess = CpuAccessMode::None;
        bool isVolatile = false;
        bool resolves = false;
        CpuAccessMode::Enum effectiveAccess = CpuAccessMode::None;
    };
    static constexpr CpuAccessCase s_CpuAccessCases[] = {
        { CpuAccessMode::None, false, true, CpuAccessMode::None },
        { CpuAccessMode::Read, false, true, CpuAccessMode::Read },
        { CpuAccessMode::Write, false, true, CpuAccessMode::Write },
        { static_cast<CpuAccessMode::Enum>(UINT8_MAX), false, false, CpuAccessMode::None },
        { CpuAccessMode::None, true, true, CpuAccessMode::Write },
        { CpuAccessMode::Read, true, false, CpuAccessMode::None },
        { CpuAccessMode::Write, true, true, CpuAccessMode::Write },
        { static_cast<CpuAccessMode::Enum>(UINT8_MAX), true, false, CpuAccessMode::None },
    };
    static constexpr HeapType::Enum s_HeapTypes[] = {
        HeapType::DeviceLocal,
        HeapType::Upload,
        HeapType::Readback,
        static_cast<HeapType::Enum>(UINT8_MAX),
    };

    for(usize accessIndex = 0u; accessIndex < LengthOf(s_CpuAccessCases); ++accessIndex){
        const CpuAccessCase& accessCase = s_CpuAccessCases[accessIndex];
        CpuAccessMode::Enum effectiveAccess = CpuAccessMode::None;
        EXPECT_EQ(
            Binding::TryResolveBufferCpuAccess(
                accessCase.declaredAccess,
                accessCase.isVolatile,
                effectiveAccess
            ),
            accessCase.resolves
        ) << "CPU access case " << accessIndex;
        EXPECT_EQ(effectiveAccess, accessCase.effectiveAccess) << "CPU access case " << accessIndex;

        for(usize heapIndex = 0u; heapIndex < LengthOf(s_HeapTypes); ++heapIndex){
            const HeapType::Enum heapType = s_HeapTypes[heapIndex];
            const bool expectedCompatibility =
                accessCase.resolves
                && (
                    (accessCase.effectiveAccess == CpuAccessMode::None && heapType == HeapType::DeviceLocal)
                    || (accessCase.effectiveAccess == CpuAccessMode::Write && heapType == HeapType::Upload)
                    || (accessCase.effectiveAccess == CpuAccessMode::Read && heapType == HeapType::Readback)
                )
            ;
            EXPECT_EQ(
                Binding::IsBufferHeapTypeCompatible(
                    accessCase.declaredAccess,
                    accessCase.isVolatile,
                    heapType
                ),
                expectedCompatibility
            ) << "CPU access case " << accessIndex << ", heap case " << heapIndex;
        }
    }
}

TEST(HeapBindingContract, PadsReadbackRequirementsToWholeNonCoherentAtoms){
    const MemoryRequirements nativeRequirements{
        .size = 257u,
        .alignment = 64u,
    };
    MemoryRequirements adjustedRequirements;
    ASSERT_TRUE(Binding::TryBuildBufferHeapRequirements(
        nativeRequirements,
        CpuAccessMode::Read,
        false,
        256u,
        adjustedRequirements
    ));
    EXPECT_EQ(adjustedRequirements.size, 512u);
    EXPECT_EQ(adjustedRequirements.alignment, 256u);

    Binding::HeapBindingRange readbackRange;
    Binding::HeapBindingRange adjacentRange;
    ASSERT_TRUE(Binding::TryBuildHeapBindingRange(
        2048u,
        0u,
        0u,
        adjustedRequirements.size,
        adjustedRequirements.alignment,
        readbackRange
    ));
    ASSERT_TRUE(Binding::TryBuildHeapBindingRange(2048u, 0u, 257u, 64u, 1u, adjacentRange));
    EXPECT_TRUE(Binding::HeapBindingRangesConflict(
        readbackRange,
        Binding::HeapBindingResourceClass::Buffer,
        adjacentRange,
        Binding::HeapBindingResourceClass::OptimalImage,
        1u
    ));
    ASSERT_TRUE(Binding::TryBuildHeapBindingRange(2048u, 0u, 512u, 64u, 1u, adjacentRange));
    EXPECT_FALSE(Binding::HeapBindingRangesConflict(
        readbackRange,
        Binding::HeapBindingResourceClass::Buffer,
        adjacentRange,
        Binding::HeapBindingResourceClass::OptimalImage,
        1u
    ));

    ASSERT_TRUE(Binding::TryBuildBufferHeapRequirements(
        nativeRequirements,
        CpuAccessMode::Write,
        false,
        256u,
        adjustedRequirements
    ));
    EXPECT_EQ(adjustedRequirements.size, nativeRequirements.size);
    EXPECT_EQ(adjustedRequirements.alignment, nativeRequirements.alignment);

    const MemoryRequirements overflowingRequirements{
        .size = Limit<u64>::s_Max,
        .alignment = 1u,
    };
    EXPECT_FALSE(Binding::TryBuildBufferHeapRequirements(
        overflowingRequirements,
        CpuAccessMode::Read,
        false,
        256u,
        adjustedRequirements
    ));
    EXPECT_EQ(adjustedRequirements.size, 0u);
    EXPECT_EQ(adjustedRequirements.alignment, 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

