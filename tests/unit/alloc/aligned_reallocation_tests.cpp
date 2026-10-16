// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/alloc/general.h>
#include <core/alloc/persistent.h>

#include <tests/common/test_context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_aligned_reallocation_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_VerifyAlignedReallocationAlignment = 256u;
inline constexpr usize s_VerifyAlignedReallocationInitialBytes = s_VerifyAlignedReallocationAlignment;
inline constexpr usize s_VerifyAlignedReallocationBlockerBytes = s_VerifyAlignedReallocationAlignment;
inline constexpr usize s_VerifyAlignedReallocationGrownBytes = s_VerifyAlignedReallocationAlignment * 4u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Arena>
static void VerifyAlignedReallocation(Arena& arena){
    constexpr usize s_Alignment = s_VerifyAlignedReallocationAlignment;
    constexpr usize s_InitialBytes = s_VerifyAlignedReallocationInitialBytes;
    constexpr usize s_BlockerBytes = s_VerifyAlignedReallocationBlockerBytes;
    constexpr usize s_GrownBytes = s_VerifyAlignedReallocationGrownBytes;

    EXPECT_EQ(arena.reallocate(nullptr, s_Alignment, 0u), nullptr);
    auto* initial = static_cast<u8*>(arena.reallocate(nullptr, s_Alignment, s_InitialBytes));
    ASSERT_NE(initial, nullptr);
    EXPECT_EQ(reinterpret_cast<usize>(initial) % s_Alignment, 0u);
    for(usize i = 0u; i < s_InitialBytes; ++i)
        initial[i] = static_cast<u8>(i);

    void* blocker = arena.allocate(1u, s_BlockerBytes);
    ASSERT_NE(blocker, nullptr);

    auto* resized = static_cast<u8*>(arena.reallocate(initial, s_Alignment, s_GrownBytes));
    ASSERT_NE(resized, nullptr);
    EXPECT_EQ(reinterpret_cast<usize>(resized) % s_Alignment, 0u);
    for(usize i = 0u; i < s_InitialBytes; ++i)
        EXPECT_EQ(resized[i], static_cast<u8>(i));

    arena.deallocate(blocker, 1u, s_BlockerBytes);
    EXPECT_EQ(arena.reallocate(resized, s_Alignment, 0u), nullptr);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(ArenaAlignedReallocation, GlobalArenaReallocationPreservesAlignment){
    NWB::Core::Alloc::GlobalArena arena(NWB::Tests::s_TestArena);
    VerifyAlignedReallocation(arena);
}

TEST(ArenaAlignedReallocation, PersistentArenaReallocationPreservesAlignment){
    NWB::Core::Alloc::PersistentArena arena(NWB::Tests::s_TestArena, NWB::Core::Alloc::PersistentArena::StructureAlignedSize(16u * 1024u));
    VerifyAlignedReallocation(arena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

