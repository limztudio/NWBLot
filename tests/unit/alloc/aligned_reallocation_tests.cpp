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
inline constexpr usize s_VerifyAlignedReallocationGrownBytes = s_VerifyAlignedReallocationAlignment * 4u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Arena>
static void VerifyAlignedReallocation(Arena& arena){
    EXPECT_EQ(arena.reallocate(nullptr, s_VerifyAlignedReallocationAlignment, 0u), nullptr);
    auto* initial = static_cast<u8*>(arena.reallocate(nullptr, s_VerifyAlignedReallocationAlignment, s_VerifyAlignedReallocationAlignment));
    ASSERT_NE(initial, nullptr);
    EXPECT_EQ(reinterpret_cast<usize>(initial) % s_VerifyAlignedReallocationAlignment, 0u);
    for(usize i = 0u; i < s_VerifyAlignedReallocationAlignment; ++i)
        initial[i] = static_cast<u8>(i);

    void* blocker = arena.allocate(1u, s_VerifyAlignedReallocationAlignment);
    ASSERT_NE(blocker, nullptr);

    auto* resized = static_cast<u8*>(arena.reallocate(initial, s_VerifyAlignedReallocationAlignment, s_VerifyAlignedReallocationGrownBytes));
    ASSERT_NE(resized, nullptr);
    EXPECT_EQ(reinterpret_cast<usize>(resized) % s_VerifyAlignedReallocationAlignment, 0u);
    for(usize i = 0u; i < s_VerifyAlignedReallocationAlignment; ++i)
        EXPECT_EQ(resized[i], static_cast<u8>(i));

    arena.deallocate(blocker, 1u, s_VerifyAlignedReallocationAlignment);
    EXPECT_EQ(arena.reallocate(resized, s_VerifyAlignedReallocationAlignment, 0u), nullptr);
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

