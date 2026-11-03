// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/caustic/settings.adhd.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_caustic_resolve_activity_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;

TEST(CausticResolveActivityTests, PartialTilesReserveOneWordForEveryDispatchGroup){
    const Expected<CausticResolveActivityLayout> layout = TryResolveCausticActivityLayout(65u, 17u);
    ASSERT_TRUE(layout);
    EXPECT_EQ(layout->tilesX, 9u);
    EXPECT_EQ(layout->tilesY, 3u);
    EXPECT_EQ(layout->bufferByteSize, 27u * sizeof(u32));
}

TEST(CausticResolveActivityTests, InvalidExtentsNeverPublishLayout){
    constexpr Array<Array<u32, 2>, 4> extents = {{
        { 0u, 1u },
        { 1u, 0u },
        { Limit<u32>::s_Max, 1u },
        { Limit<u32>::s_Max, Limit<u32>::s_Max },
    }};
    for(const auto& extent : extents){
        EXPECT_FALSE(TryResolveCausticActivityLayout(extent[0], extent[1]));
    }
}

TEST(CausticResolveActivityTests, LastRawWordMayEndAtTheUintAddressBoundary){
    const Expected<CausticResolveActivityLayout> layout = TryResolveCausticActivityLayout(262144u, 262144u);
    ASSERT_TRUE(layout);
    EXPECT_EQ(layout->bufferByteSize, static_cast<u64>(Limit<u32>::s_Max) + 1u);
    EXPECT_EQ(layout->tilesX, 32768u);
    EXPECT_EQ(layout->tilesY, 32768u);
    EXPECT_FALSE(TryResolveCausticActivityLayout(262145u, 262144u));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

