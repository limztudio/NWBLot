// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/caustic_resolve_activity.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_caustic_resolve_activity_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;

TEST(CausticResolveActivityTests, PartialTilesReserveOneWordForEveryDispatchGroup){
    auto layout = ResolveCausticActivityLayout(65u, 17u);
    ASSERT_TRUE(layout);
    EXPECT_EQ(layout->tilesX, 9u);
    EXPECT_EQ(layout->tilesY, 3u);
    EXPECT_EQ(layout->bufferByteSize, 27u * sizeof(u32));
    layout = ResolveCausticActivityLayout(640u, 450u);
    ASSERT_TRUE(layout);
    EXPECT_EQ(layout->tilesX, 80u);
    EXPECT_EQ(layout->tilesY, 57u);
    EXPECT_EQ(layout->bufferByteSize, 18240u);
}

TEST(CausticResolveActivityTests, InvalidExtentsNeverPublishLayout){
    const u32 extents[][2] = { { 0u, 1u }, { 1u, 0u }, { Limit<u32>::s_Max, 1u }, { Limit<u32>::s_Max, Limit<u32>::s_Max } };
    for(const auto& extent : extents){
        EXPECT_FALSE(ResolveCausticActivityLayout(extent[0], extent[1]));
    }
}

TEST(CausticResolveActivityTests, LastRawWordMayEndAtTheUintAddressBoundary){
    const auto layout = ResolveCausticActivityLayout(262144u, 262144u);
    ASSERT_TRUE(layout);
    EXPECT_EQ(layout->bufferByteSize, static_cast<u64>(Limit<u32>::s_Max) + 1u);
    EXPECT_EQ(layout->tilesX, 32768u);
    EXPECT_EQ(layout->tilesY, 32768u);
    EXPECT_FALSE(ResolveCausticActivityLayout(262145u, 262144u));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

