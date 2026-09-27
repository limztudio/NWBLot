// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/gi/quality_settings.h>

#include <global/limit.h>
#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_surfel_gi_quality_settings_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Impl;

TEST(SurfelGiQualitySettings, DefaultsRetainHalfAndUnsupportedScalesAreRejected){
    const SurfelGiQualitySettings defaults;
    EXPECT_EQ(defaults.resolveResolution, SurfelGiResolveResolution::Half);
    EXPECT_TRUE(ValidateSurfelGiQualitySettings(defaults));
    EXPECT_TRUE(ValidateSurfelGiQualitySettings({ SurfelGiResolveResolution::Quarter }));
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
    const SurfelGiResolveSize resized = MakeSurfelGiResolveSize(1001u, 701u, 4u);
    EXPECT_EQ(resized.width, 251u);
    EXPECT_EQ(resized.height, 176u);
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

