// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/graphics/vulkan/state_tracking_detail.h>

#include <tests/common/graphics_metadata_test_objects.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_buffer_range_state_tracking_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
namespace Core = NWB::Core;
namespace Backend = Core::GraphicsBackend;

struct RangeContext{
    Core::Alloc::GlobalArena arena{ Name("tests/buffer_range_state_tracking") };
    Core::GraphicsAllocator graphicsAllocator{ arena };
    Core::CpuTaskScheduler cpuScheduler{ 0u };
    Backend::VulkanContext context{ graphicsAllocator, cpuScheduler, 1u };
    Backend::VulkanAllocator allocator{ context };
    Core::BufferHandle buffer;
    Backend::StateTracker tracker{ context };

    explicit RangeContext(const bool retained = false, const u64 byteSize = 256u)
        : buffer(
            Tests::NewMetadataOnlyBuffer(
                arena,
                context,
                allocator,
                Core::BufferDesc{}.setByteSize(byteSize).setInitialState(Core::ResourceStates::ShaderResource).setKeepInitialState(retained),
                retained
            ),
            Core::BufferHandle::deleter_type(&arena),
            AdoptRef
        )
    {}
};


TEST(BufferRangeStateTracking, DisjointUpdatesPreserveUnknownGaps){
    RangeContext test;
    test.tracker.beginTrackingBuffer(test.buffer.get(), Core::ResourceStates::CopyDest, { 16u, 32u });
    test.tracker.beginTrackingBuffer(test.buffer.get(), Core::ResourceStates::ShaderResource, { 128u, 64u });

    EXPECT_EQ(test.tracker.getBufferState(test.buffer.get(), { 16u, 32u }), Core::ResourceStates::CopyDest);
    EXPECT_EQ(test.tracker.getBufferState(test.buffer.get(), { 128u, 64u }), Core::ResourceStates::ShaderResource);
    EXPECT_EQ(test.tracker.getBufferState(test.buffer.get(), { 48u, 80u }), Core::ResourceStates::Unknown);
    EXPECT_EQ(test.tracker.getBufferState(test.buffer.get()), Core::ResourceStates::Unknown);
    EXPECT_TRUE(test.tracker.hasExplicitBufferState(test.buffer.get(), { 16u, 32u }));
    EXPECT_FALSE(test.tracker.hasExplicitBufferState(test.buffer.get(), { 16u, 176u }));
    EXPECT_FALSE(test.tracker.hasExplicitBufferState(test.buffer.get()));
}

TEST(BufferRangeStateTracking, PartialOverlapPreservesBothSidesAndWholeTransitionsUnifyState){
    RangeContext test;
    test.tracker.beginTrackingBuffer(test.buffer.get(), Core::ResourceStates::CopySource);
    test.tracker.beginTrackingBuffer(test.buffer.get(), Core::ResourceStates::CopyDest, { 64u, 128u });
    test.tracker.beginTrackingBuffer(test.buffer.get(), Core::ResourceStates::ShaderResource, { 96u, 32u });

    EXPECT_EQ(test.tracker.getBufferState(test.buffer.get(), { 0u, 64u }), Core::ResourceStates::CopySource);
    EXPECT_EQ(test.tracker.getBufferState(test.buffer.get(), { 64u, 32u }), Core::ResourceStates::CopyDest);
    EXPECT_EQ(test.tracker.getBufferState(test.buffer.get(), { 96u, 32u }), Core::ResourceStates::ShaderResource);
    EXPECT_EQ(test.tracker.getBufferState(test.buffer.get(), { 128u, 64u }), Core::ResourceStates::CopyDest);
    EXPECT_EQ(test.tracker.getBufferState(test.buffer.get(), { 192u, 64u }), Core::ResourceStates::CopySource);
    EXPECT_EQ(test.tracker.getBufferState(test.buffer.get()), Core::ResourceStates::Unknown);
    EXPECT_TRUE(test.tracker.hasExplicitBufferState(test.buffer.get()));
    EXPECT_TRUE(test.tracker.hasExplicitBufferState(test.buffer.get(), Core::s_EntireBuffer, true));

    test.tracker.beginTrackingBuffer(test.buffer.get(), Core::ResourceStates::UnorderedAccess);
    EXPECT_EQ(test.tracker.getBufferState(test.buffer.get()), Core::ResourceStates::UnorderedAccess);
    EXPECT_EQ(test.tracker.getBufferState(test.buffer.get(), { 255u, Core::BufferRange::AllBytes }), Core::ResourceStates::UnorderedAccess);
}

TEST(BufferRangeStateTracking, RetainedFallbackDoesNotClaimExplicitCoverage){
    RangeContext test(true);
    test.tracker.beginTrackingBuffer(test.buffer.get(), Core::ResourceStates::CopyDest, { 64u, 64u });

    EXPECT_EQ(test.tracker.getBufferState(test.buffer.get(), { 0u, 64u }), Core::ResourceStates::ShaderResource);
    EXPECT_EQ(test.tracker.getBufferState(test.buffer.get(), { 128u, 128u }), Core::ResourceStates::ShaderResource);
    EXPECT_EQ(test.tracker.getBufferState(test.buffer.get()), Core::ResourceStates::Unknown);
    EXPECT_FALSE(test.tracker.hasExplicitBufferState(test.buffer.get()));
    EXPECT_TRUE(test.tracker.hasExplicitBufferState(test.buffer.get(), { 64u, 64u }));

    test.tracker.beginTrackingBuffer(test.buffer.get(), Core::ResourceStates::ShaderResource, { 64u, 64u });
    EXPECT_EQ(test.tracker.getBufferState(test.buffer.get()), Core::ResourceStates::ShaderResource);
    EXPECT_FALSE(test.tracker.hasExplicitBufferState(test.buffer.get()));
}

TEST(BufferRangeStateTracking, EmptyAndOverflowingUpdatesDoNotPublishState){
    RangeContext test;
    test.tracker.beginTrackingBuffer(test.buffer.get(), Core::ResourceStates::CopyDest, { 64u, 0u });
    test.tracker.beginTrackingBuffer(test.buffer.get(), Core::ResourceStates::CopyDest, { 256u, 16u });
    test.tracker.beginTrackingBuffer(test.buffer.get(), Core::ResourceStates::CopyDest, { 64u, 999u });
    test.tracker.beginTrackingBuffer(test.buffer.get(), Core::ResourceStates::CopyDest, { Core::BufferRange::AllBytes - 3u, 8u });

    EXPECT_FALSE(test.tracker.hasExplicitBufferState(test.buffer.get()));
    EXPECT_EQ(test.tracker.getBufferState(test.buffer.get()), Core::ResourceStates::Unknown);
    EXPECT_EQ(test.tracker.getBufferState(test.buffer.get(), { 0u, 0u }), Core::ResourceStates::Unknown);
}

TEST(BufferRangeStateTracking, UnknownUpdateReplacesMixedKnownBytesWithoutLosingOutsideState){
    RangeContext test(false, 128u);
    test.tracker.beginTrackingBuffer(test.buffer.get(), Core::ResourceStates::CopySource);
    test.tracker.beginTrackingBuffer(test.buffer.get(), Core::ResourceStates::CopyDest, { 32u, 64u });
    ASSERT_EQ(test.tracker.getBufferState(test.buffer.get(), { 16u, 96u }), Core::ResourceStates::Unknown);
    ASSERT_TRUE(test.tracker.hasExplicitBufferState(test.buffer.get(), { 16u, 96u }, true));

    test.tracker.beginTrackingBuffer(test.buffer.get(), Core::ResourceStates::Unknown, { 16u, 96u });

    EXPECT_EQ(test.tracker.getBufferState(test.buffer.get(), { 0u, 16u }), Core::ResourceStates::CopySource);
    EXPECT_EQ(test.tracker.getBufferState(test.buffer.get(), { 16u, 16u }), Core::ResourceStates::Unknown);
    EXPECT_EQ(test.tracker.getBufferState(test.buffer.get(), { 32u, 64u }), Core::ResourceStates::Unknown);
    EXPECT_EQ(test.tracker.getBufferState(test.buffer.get(), { 96u, 16u }), Core::ResourceStates::Unknown);
    EXPECT_EQ(test.tracker.getBufferState(test.buffer.get(), { 112u, 16u }), Core::ResourceStates::CopySource);
    EXPECT_TRUE(test.tracker.hasExplicitBufferState(test.buffer.get()));
    EXPECT_FALSE(test.tracker.hasExplicitBufferState(test.buffer.get(), Core::s_EntireBuffer, true));
    EXPECT_FALSE(test.tracker.hasExplicitBufferState(test.buffer.get(), { 16u, 96u }, true));
}

TEST(BufferRangeStateTracking, MatchesPerByteModelAcrossDeterministicOverlappingUpdates){
    constexpr u64 s_ByteCount = 128u;
    RangeContext test(false, s_ByteCount);
    Core::ResourceStates::Mask expectedStates[s_ByteCount]{};
    bool expectedExplicit[s_ByteCount]{};
    constexpr Core::BufferRange s_InitialRanges[] = {
        { 16u, 64u },
        { 32u, 16u },
        { 80u, 16u },
        { 0u, 16u },
        { 8u, 104u },
        { 112u, 16u },
        { 0u, s_ByteCount },
        { 64u, 64u },
        { 0u, 64u },
        { 127u, 1u },
    };
    constexpr Core::ResourceStates::Mask s_States[] = {
        Core::ResourceStates::CopyDest,
        Core::ResourceStates::ShaderResource,
        Core::ResourceStates::CopySource,
        Core::ResourceStates::UnorderedAccess,
    };
    u32 sequence = 0x3579bdu;
    for(u32 operationIndex = 0u; operationIndex < 138u; ++operationIndex){
        SCOPED_TRACE(operationIndex);
        sequence = sequence * 1664525u + 1013904223u;
        const u64 offset = (sequence >> 8u) % s_ByteCount;
        const u64 size = 1u + (sequence >> 16u) % (s_ByteCount - offset);
        Core::BufferRange range(offset, size);
        if(operationIndex < LengthOf(s_InitialRanges))
            range = s_InitialRanges[operationIndex];
        else if(operationIndex % 17u == 0u)
            range = Core::BufferRange(0u, s_ByteCount);
        const Core::ResourceStates::Mask state = s_States[operationIndex % LengthOf(s_States)];
        test.tracker.beginTrackingBuffer(test.buffer.get(), state, range);
        for(u64 byte = range.byteOffset; byte < range.byteOffset + range.byteSize; ++byte){
            expectedStates[byte] = state;
            expectedExplicit[byte] = true;
        }

        Core::ResourceStates::Mask expectedWhole = expectedStates[0u];
        bool completeExplicit = true;
        bool completeKnown = true;
        for(u64 byte = 0u; byte < s_ByteCount; ++byte){
            SCOPED_TRACE(byte);
            EXPECT_EQ(test.tracker.getBufferState(test.buffer.get(), { byte, 1u }), expectedStates[byte]);
            EXPECT_EQ(test.tracker.hasExplicitBufferState(test.buffer.get(), { byte, 1u }), expectedExplicit[byte]);
            const bool known = expectedExplicit[byte] && expectedStates[byte] != Core::ResourceStates::Unknown;
            EXPECT_EQ(test.tracker.hasExplicitBufferState(test.buffer.get(), { byte, 1u }, true), known);
            completeExplicit = completeExplicit && expectedExplicit[byte];
            completeKnown = completeKnown && known;
            if(expectedStates[byte] != expectedStates[0u])
                expectedWhole = Core::ResourceStates::Unknown;
        }
        EXPECT_EQ(test.tracker.getBufferState(test.buffer.get()), expectedWhole);
        EXPECT_EQ(test.tracker.hasExplicitBufferState(test.buffer.get()), completeExplicit);
        EXPECT_EQ(test.tracker.hasExplicitBufferState(test.buffer.get(), Core::s_EntireBuffer, true), completeKnown);
    }
}

TEST(BufferRangeStateTracking, StateAndOwnershipBarriersKeepTheExactByteInterval){
    const Core::BufferRange range{ 37u, 91u };
    const auto transition = Backend::VulkanStateTrackingDetail::BuildBufferStateBarrier(
        VK_NULL_HANDLE, range, Core::ResourceStates::CopyDest, Core::ResourceStates::CopySource, false
    );
    const auto release = Backend::VulkanStateTrackingDetail::BuildBufferOwnershipReleaseBarrier(
        VK_NULL_HANDLE, Core::ResourceStates::CopyDest, 2u, 5u, false, range
    );
    const auto acquire = Backend::VulkanStateTrackingDetail::BuildBufferOwnershipAcquireBarrier(
        VK_NULL_HANDLE, Core::ResourceStates::CopyDest, 2u, 5u, false, range
    );

    EXPECT_EQ(transition.offset, range.byteOffset);
    EXPECT_EQ(transition.size, range.byteSize);
    EXPECT_EQ(transition.srcAccessMask, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    EXPECT_EQ(transition.dstAccessMask, VK_ACCESS_2_TRANSFER_READ_BIT);
    EXPECT_EQ(release.offset, range.byteOffset);
    EXPECT_EQ(release.size, range.byteSize);
    EXPECT_EQ(acquire.offset, release.offset);
    EXPECT_EQ(acquire.size, release.size);
    EXPECT_EQ(release.dstStageMask, VK_PIPELINE_STAGE_2_NONE);
    EXPECT_EQ(acquire.srcStageMask, VK_PIPELINE_STAGE_2_NONE);
    EXPECT_EQ(release.srcQueueFamilyIndex, acquire.srcQueueFamilyIndex);
    EXPECT_EQ(release.dstQueueFamilyIndex, acquire.dstQueueFamilyIndex);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

