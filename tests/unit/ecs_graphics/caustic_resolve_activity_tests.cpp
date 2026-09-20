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
    CausticResolveActivityLayout layout;
    ASSERT_TRUE(ResolveCausticActivityLayout(65u, 17u, layout));
    EXPECT_EQ(layout.tilesX, 9u);
    EXPECT_EQ(layout.tilesY, 3u);
    EXPECT_EQ(layout.bufferByteSize, 27u * sizeof(u32));
    ASSERT_TRUE(ResolveCausticActivityLayout(640u, 450u, layout));
    EXPECT_EQ(layout.tilesX, 80u);
    EXPECT_EQ(layout.tilesY, 57u);
    EXPECT_EQ(layout.bufferByteSize, 18240u);
}

TEST(CausticResolveActivityTests, InvalidExtentsResetPreviouslyResolvedLayout){
    const u32 extents[][2] = { { 0u, 1u }, { 1u, 0u }, { Limit<u32>::s_Max, 1u }, { Limit<u32>::s_Max, Limit<u32>::s_Max } };
    for(const auto& extent : extents){
        CausticResolveActivityLayout layout{ 123u, 4u, 5u };
        EXPECT_FALSE(ResolveCausticActivityLayout(extent[0], extent[1], layout));
        EXPECT_EQ(layout.bufferByteSize, 0u);
        EXPECT_EQ(layout.tilesX, 0u);
        EXPECT_EQ(layout.tilesY, 0u);
    }
}

TEST(CausticResolveActivityTests, LastRawWordMayEndAtTheUintAddressBoundary){
    CausticResolveActivityLayout layout;
    ASSERT_TRUE(ResolveCausticActivityLayout(262144u, 262144u, layout));
    EXPECT_EQ(layout.bufferByteSize, static_cast<u64>(Limit<u32>::s_Max) + 1u);
    EXPECT_EQ(layout.tilesX, 32768u);
    EXPECT_EQ(layout.tilesY, 32768u);
    EXPECT_FALSE(ResolveCausticActivityLayout(262145u, 262144u, layout));
    EXPECT_EQ(layout.bufferByteSize, 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

