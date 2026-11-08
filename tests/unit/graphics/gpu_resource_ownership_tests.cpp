// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/graphics/rhi/ownership.adhd.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(ResourceStateOwnership, MatchesReleaseDestinationOnlyForTheExpectedQueue){
    const Core::GpuPhysicalQueueId owner{ .index = 1u, .deviceGeneration = 7u };
    const Core::GpuPhysicalQueueId other{ .index = 2u, .deviceGeneration = 7u };
    const Core::GpuPhysicalQueueId oldGeneration{ .index = 2u, .deviceGeneration = 6u };
    const Core::GpuPhysicalQueueId invalid{};
    EXPECT_TRUE(Core::MatchesResourceStateOwnership(owner, invalid, owner, owner));
    EXPECT_FALSE(Core::MatchesResourceStateOwnership(owner, other, owner, owner));
    EXPECT_TRUE(Core::MatchesResourceStateOwnership(owner, other, owner, other));
    EXPECT_FALSE(Core::MatchesResourceStateOwnership(owner, invalid, owner, other));
    EXPECT_FALSE(Core::MatchesResourceStateOwnership(owner, oldGeneration, owner, other));
    EXPECT_FALSE(Core::MatchesResourceStateOwnership(other, invalid, owner, owner));
    EXPECT_TRUE(Core::MatchesResourceStateOwnership(invalid, invalid, invalid, invalid));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

