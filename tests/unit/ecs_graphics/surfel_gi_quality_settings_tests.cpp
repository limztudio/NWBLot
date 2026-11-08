// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/gi/lifecycle_types.adhd.h>
#include <impl/ecs_render/gi/quality_settings.adhd.h>

#include <global/limit.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_surfel_gi_quality_settings_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Impl;

TEST(SurfelGiQualitySettings, ReadbackTokenRequiresMatchingPhysicalQueueIdentity){
    const NWB::Core::QueueSubmissionToken accepted{ .value = 7u, .physicalQueueIndex = 2u, .deviceGeneration = 3u, .queue = NWB::Core::CommandQueue::Compute };
    EXPECT_TRUE(SurfelReadbackSubmissionMatches(accepted, accepted));
    EXPECT_FALSE(SurfelReadbackSubmissionMatches({}, accepted));
    auto candidate = accepted;
    candidate.physicalQueueIndex = 4u;
    EXPECT_FALSE(SurfelReadbackSubmissionMatches(accepted, candidate));
    candidate = accepted;
    candidate.deviceGeneration = 0u;
    EXPECT_FALSE(SurfelReadbackSubmissionMatches(accepted, candidate));
    candidate = accepted;
    candidate.queue = NWB::Core::CommandQueue::Graphics;
    EXPECT_FALSE(SurfelReadbackSubmissionMatches(accepted, candidate));
    candidate = accepted;
    candidate.value = 0u;
    EXPECT_FALSE(SurfelReadbackSubmissionMatches(accepted, candidate));
}

TEST(SurfelGiQualitySettings, SpawnGroupsHandleOddZeroAndMaximumExtents){
    EXPECT_EQ(ResolveSurfelSpawnDispatchGroup(0u, 8u), 0u);
    EXPECT_EQ(ResolveSurfelSpawnDispatchGroup(128u, 8u), 1u);
    EXPECT_EQ(ResolveSurfelSpawnDispatchGroup(129u, 8u), 2u);
    EXPECT_EQ(ResolveSurfelSpawnDispatchGroup(Limit<u32>::s_Max, 8u), 33554432u);
    EXPECT_EQ(ResolveSurfelSpawnDispatchGroup(129u, 0u), 0u);
}

TEST(SurfelGiQualitySettings, StorageCountsAndResolveExtentsRejectEitherEmptyDimension){
    EXPECT_FALSE(IsValidSurfelGiStorageCounts(0u, 1u));
    EXPECT_FALSE(IsValidSurfelGiStorageCounts(1u, 0u));
    EXPECT_TRUE(IsValidSurfelGiStorageCounts(Limit<u32>::s_Max, Limit<u32>::s_Max));
    EXPECT_FALSE(IsValidSurfelGiResolveSize(MakeSurfelGiResolveSize(0u, 1u, 2u)));
    EXPECT_FALSE(IsValidSurfelGiResolveSize(MakeSurfelGiResolveSize(1u, 0u, 2u)));
    EXPECT_TRUE(IsValidSurfelGiResolveSize(MakeSurfelGiResolveSize(1u, 1u, 2u)));
    EXPECT_EQ(SurfelGiResolveSize::reflectedMethodCount(), 0u);
}

TEST(SurfelGiQualitySettings, BufferByteSizeKeepsWideMultiplicationAndWrapping){
    EXPECT_EQ(ComputeSurfelBufferByteSize(96u, Limit<u32>::s_Max), 96ull * static_cast<u64>(Limit<u32>::s_Max));
    EXPECT_EQ(ComputeSurfelBufferByteSize(Limit<u64>::s_Max, 2u), Limit<u64>::s_Max - 1u);
    EXPECT_EQ(ComputeSurfelBufferByteSize(Limit<u64>::s_Max, 0u), 0u);
}

TEST(SurfelGiQualitySettings, ResolveFactorDoesNotTruncateToTheEnumWidth){
    EXPECT_TRUE(IsValidSurfelGiResolveFactor(2u));
    EXPECT_TRUE(IsValidSurfelGiResolveFactor(4u));
    EXPECT_FALSE(IsValidSurfelGiResolveFactor(258u));
    EXPECT_FALSE(IsValidSurfelGiResolveFactor(260u));
    EXPECT_FALSE(IsValidSurfelGiResolveFactor(Limit<u32>::s_Max));
}

TEST(SurfelGiQualitySettings, RejectsUnsupportedResolveScales){
    for(const u32 factor : { 0u, 1u, 3u, 5u, 255u })
        EXPECT_FALSE(ValidateSurfelGiQualitySettings({ static_cast<SurfelGiResolveResolution::Enum>(factor) }));
}

TEST(SurfelGiQualitySettings, OddResizeKeepsEveryFullPixelInsideTheGatherGrid){
    for(const u32 factor : { 2u, 4u }){
        for(const u32 width : { 1u, 3u, 4u, 5u, 1000u, 1001u }){
            for(const u32 height : { 1u, 3u, 4u, 5u, 700u, 701u }){
                const SurfelGiResolveSize size = MakeSurfelGiResolveSize(width, height, factor);
                ASSERT_GT(size.width, 0u);
                ASSERT_GT(size.height, 0u);
                EXPECT_LT((size.width - 1u) * factor, width);
                EXPECT_LT((size.height - 1u) * factor, height);
                EXPECT_GE(size.width * factor, width);
                EXPECT_GE(size.height * factor, height);
                EXPECT_LT((width - 1u) / factor, size.width);
                EXPECT_LT((height - 1u) / factor, size.height);
            }
        }
    }
}

TEST(SurfelGiQualitySettings, EmptyAndLargestExtentsDoNotWrapTheReducedSize){
    for(const u32 factor : { 2u, 4u }){
        const SurfelGiResolveSize empty = MakeSurfelGiResolveSize(0u, 0u, factor);
        EXPECT_EQ(empty.width, 0u);
        EXPECT_EQ(empty.height, 0u);
        const SurfelGiResolveSize largest = MakeSurfelGiResolveSize(Limit<u32>::s_Max, Limit<u32>::s_Max, factor);
        EXPECT_GE(static_cast<u64>(largest.width) * factor, static_cast<u64>(Limit<u32>::s_Max));
        EXPECT_LT(static_cast<u64>(largest.width - 1u) * factor, static_cast<u64>(Limit<u32>::s_Max));
        EXPECT_EQ(largest.width, largest.height);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

