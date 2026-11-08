// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>

#include <global/atomic_identity.h>
#include <global/limit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_atomic_identity_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Global, AtomicIdentityAndHighWaterRespectLimits){
    Atomic<u64> highWater{ Limit<u64>::s_Max - 1u };
    UpdateAtomicMaxRelaxed(highWater, u64(0));
    EXPECT_EQ(highWater.load(MemoryOrder::relaxed), Limit<u64>::s_Max - 1u);
    UpdateAtomicMaxRelaxed(highWater, Limit<u64>::s_Max);
    EXPECT_EQ(DecrementAtomicOrTerminate(highWater), Limit<u64>::s_Max);
    EXPECT_EQ(highWater.load(MemoryOrder::relaxed), Limit<u64>::s_Max - 1u);
    Atomic<u64> identities{ Limit<u64>::s_Max - 1u };
    EXPECT_EQ(TryNextAtomicIdentity(identities), Limit<u64>::s_Max - 1u);
    EXPECT_EQ(TryNextAtomicIdentity(identities), 0u);
    EXPECT_EQ(identities.load(MemoryOrder::relaxed), Limit<u64>::s_Max);
    Atomic<u32> boundedIdentity{ Limit<u16>::s_Max };
    const auto lastBoundedIdentity = TryNextAtomicBoundedIdentity<u16>(boundedIdentity);
    ASSERT_TRUE(lastBoundedIdentity);
    EXPECT_EQ(*lastBoundedIdentity, Limit<u16>::s_Max);
    EXPECT_EQ(boundedIdentity.load(MemoryOrder::relaxed), static_cast<u32>(Limit<u16>::s_Max) + 1u);
    EXPECT_FALSE(TryNextAtomicBoundedIdentity<u16>(boundedIdentity));
    Atomic<u32> zeroBoundedIdentity{ 0u };
    EXPECT_FALSE(TryNextAtomicBoundedIdentity<u16>(zeroBoundedIdentity));
    EXPECT_EQ(zeroBoundedIdentity.load(MemoryOrder::relaxed), 0u);
    Atomic<u64> zeroIdentity{ 0u };
    EXPECT_EQ(TryNextAtomicIdentity(zeroIdentity), 0u);
    EXPECT_EQ(zeroIdentity.load(MemoryOrder::relaxed), 0u);
    const auto allowedZeroIdentity = TryNextAtomicIdentityAllowZero(zeroIdentity);
    ASSERT_TRUE(allowedZeroIdentity);
    EXPECT_EQ(*allowedZeroIdentity, 0u);
    EXPECT_EQ(zeroIdentity.load(MemoryOrder::relaxed), 1u);
    EXPECT_FALSE(TryNextAtomicIdentityAllowZero(identities));
    EXPECT_EQ(identities.load(MemoryOrder::relaxed), Limit<u64>::s_Max);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

