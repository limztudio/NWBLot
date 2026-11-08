// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/os/linux/x11/text_input_preedit.h>
#include <core/os/linux/x11/text_input_dispatch.h>
#include <core/os/linux/x11/text_input_key_history.h>
#include <core/os/text_input_text.h>

#include <tests/common/test_context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_linux_text_input_model_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core;

TEST(X11TextInputDispatchFence, CallbackCancellationDefersContextUntilOuterNativeCallReturns){
    X11TextInputDispatchFence fence;
    b8 nativeContextAlive = true;
    fence.enter();
    fence.enter();
    EXPECT_FALSE(fence.retire());
    EXPECT_TRUE(nativeContextAlive);
    if(fence.leave())
        nativeContextAlive = false;
    EXPECT_TRUE(nativeContextAlive);
    // The input method can still read its IC after its client callback and nested native call return.
    if(fence.leave())
        nativeContextAlive = false;
    EXPECT_FALSE(nativeContextAlive);
    fence.released();
    fence.enter();
    EXPECT_FALSE(fence.leave());
}

TEST(X11TextInputDispatchFence, RetirementWithoutNativeStackCanReleaseImmediately){
    X11TextInputDispatchFence fence;
    EXPECT_TRUE(fence.retire());
    fence.released();
    fence.enter();
    EXPECT_FALSE(fence.leave());
}

TEST(X11FilteredKeyHistory, ForwardedPressAndReleaseCannotReplayAfterThePhysicalRelease){
    X11FilteredKeyHistory history;
    history.recordFiltered(23u, false, 100u, 50u, 1000u);
    history.recordFiltered(23u, true, 120u, 55u, 1020u);
    EXPECT_TRUE(history.isForwardedDuplicate(23u, false, 100u, 50u, false, 1030u));
    EXPECT_TRUE(history.isForwardedDuplicate(23u, true, 120u, 55u, false, 1030u));
    EXPECT_FALSE(history.isForwardedDuplicate(23u, false, 140u, 60u, false, 1040u));
    EXPECT_FALSE(history.isForwardedDuplicate(23u, true, 150u, 65u, false, 1050u));
}

TEST(X11FilteredKeyHistory, NewRepeatsAdvanceWithoutRevivingOlderForwardedPresses){
    X11FilteredKeyHistory history;
    history.recordFiltered(23u, false, 100u, 50u, 1000u);
    EXPECT_FALSE(history.isForwardedDuplicate(23u, false, 110u, 50u, false, 1010u));
    history.recordFiltered(23u, false, 110u, 50u, 1010u);
    EXPECT_TRUE(history.isForwardedDuplicate(23u, false, 100u, 50u, false, 1020u));
    EXPECT_TRUE(history.isForwardedDuplicate(23u, false, 110u, 50u, false, 1020u));
    EXPECT_FALSE(history.isForwardedDuplicate(23u, false, 110u, 51u, false, 1020u));
    EXPECT_FALSE(history.isForwardedDuplicate(24u, false, 110u, 50u, false, 1020u));
    EXPECT_FALSE(history.isForwardedDuplicate(23u, true, 110u, 50u, false, 1020u));
}

TEST(X11FilteredKeyHistory, CoalescedAutorepeatReleaseCannotClearTheLaterRepeatedPress){
    X11FilteredKeyHistory history;
    history.recordFiltered(23u, false, 100u, 50u, 1000u);
    // Legacy X11 autorepeat coalesces this release with the adjacent press without releasing the held key.
    history.recordFiltered(23u, true, 120u, 55u, 1020u);
    history.recordFiltered(23u, false, 120u, 55u, 1020u);
    EXPECT_TRUE(history.isForwardedDuplicate(23u, true, 120u, 55u, false, 1030u));
    EXPECT_TRUE(history.isForwardedDuplicate(23u, false, 120u, 55u, false, 1030u));
    EXPECT_FALSE(history.isForwardedDuplicate(23u, true, 140u, 60u, false, 1040u));
}

TEST(X11FilteredKeyHistory, ZeroTimeSyntheticCyclesStayDistinctFromTheirForwardedCopies){
    X11FilteredKeyHistory history;
    for(u32 cycle = 0u; cycle < 3u; ++cycle){
        EXPECT_FALSE(history.isForwardedDuplicate(23u, false, 0u, 50u, true, 1000u + cycle));
        history.recordFiltered(23u, false, 0u, 50u, 1000u + cycle);
        EXPECT_FALSE(history.isForwardedDuplicate(23u, true, 0u, 50u, true, 1000u + cycle));
        history.recordFiltered(23u, true, 0u, 50u, 1000u + cycle);
        EXPECT_TRUE(history.isForwardedDuplicate(23u, false, 0u, 50u, false, 1000u + cycle));
        EXPECT_TRUE(history.isForwardedDuplicate(23u, true, 0u, 50u, false, 1000u + cycle));
    }
    EXPECT_FALSE(history.isForwardedDuplicate(23u, false, 0u, 51u, false, 1010u));
}

TEST(X11FilteredKeyHistory, TimedAndZeroTimeKeysDoNotOverwriteEachOthersHistory){
    X11FilteredKeyHistory history;
    history.recordFiltered(23u, false, 400u, 50u, 1000u);
    history.recordFiltered(23u, false, 0u, 60u, 1010u);
    EXPECT_TRUE(history.isForwardedDuplicate(23u, false, 400u, 50u, false, 1020u));
    EXPECT_TRUE(history.isForwardedDuplicate(23u, false, 0u, 60u, false, 1020u));
    EXPECT_FALSE(history.isForwardedDuplicate(23u, false, 401u, 50u, false, 1020u));
    history.recordFiltered(0u, false, 0u, 60u, 1010u);
    history.recordFiltered(256u, false, 0u, 60u, 1010u);
    EXPECT_FALSE(history.isForwardedDuplicate(0u, false, 0u, 60u, false, 1020u));
    EXPECT_FALSE(history.isForwardedDuplicate(256u, false, 0u, 60u, false, 1020u));
}

TEST(X11FilteredKeyHistory, TimestampAndSerialWrapAdmitNewInputAndRejectOldCopies){
    X11FilteredKeyHistory history;
    history.recordFiltered(23u, false, 0xfffffff0u, 50u, 1000u);
    EXPECT_FALSE(history.isForwardedDuplicate(23u, false, 5u, 50u, false, 1020u));
    history.recordFiltered(23u, false, 5u, 50u, 1020u);
    EXPECT_TRUE(history.isForwardedDuplicate(23u, false, 0xfffffff0u, 50u, false, 1030u));
    history.recordFiltered(23u, true, 0u, 0xfffffff0u, 1000u);
    EXPECT_FALSE(history.isForwardedDuplicate(23u, true, 0u, 5u, false, 1020u));
    history.recordFiltered(23u, true, 0u, 5u, 1020u);
    EXPECT_TRUE(history.isForwardedDuplicate(23u, true, 0u, 0xfffffff0u, false, 1030u));
}

TEST(X11FilteredKeyHistory, LongInactivityAndWindowResetCannotFenceFreshKeys){
    X11FilteredKeyHistory history;
    constexpr u64 s_HalfRange = 1ull << 31u;
    history.recordFiltered(23u, false, 100u, 50u, 1000u);
    EXPECT_FALSE(history.isForwardedDuplicate(23u, false, 100u, 50u, false, 1000u + s_HalfRange));
    history.recordFiltered(23u, false, 50u, 25u, 1000u + s_HalfRange);
    EXPECT_TRUE(history.isForwardedDuplicate(23u, false, 50u, 25u, false, 1001u + s_HalfRange));
    history.reset();
    EXPECT_FALSE(history.isForwardedDuplicate(23u, false, 50u, 25u, false, 1002u + s_HalfRange));
}

TEST(X11FilteredKeyHistory, SessionReplacementRejectsOldEchoesBeforeTheyCanReachANewInputContext){
    X11FilteredKeyHistory history;
    history.synchronizeSession({ 1u, 1u });
    history.recordFiltered(73u, false, 100u, 50u, 1000u);
    history.recordFiltered(73u, true, 120u, 55u, 1020u);
    history.synchronizeSession({ 1u, 1u });
    EXPECT_FALSE(history.isRetiredDuplicate(73u, false, 100u, 50u, false, 1030u));
    EXPECT_TRUE(history.isForwardedDuplicate(73u, false, 100u, 50u, false, 1030u));
    history.synchronizeSession({ 1u, 2u });
    EXPECT_TRUE(history.isRetiredDuplicate(73u, false, 100u, 50u, false, 1040u));
    EXPECT_TRUE(history.isRetiredDuplicate(73u, true, 120u, 55u, false, 1040u));
    EXPECT_FALSE(history.isRetiredDuplicate(73u, false, 140u, 60u, false, 1040u));
    history.recordFiltered(73u, false, 140u, 60u, 1040u);
    EXPECT_TRUE(history.isRetiredDuplicate(73u, false, 100u, 50u, false, 1050u));
    EXPECT_FALSE(history.isRetiredDuplicate(73u, false, 140u, 60u, false, 1050u));
    EXPECT_TRUE(history.isForwardedDuplicate(73u, false, 140u, 60u, false, 1050u));
}

TEST(X11FilteredKeyHistory, InactiveSessionGapsAndServiceReplacementKeepOldSyntheticEchoesFenced){
    X11FilteredKeyHistory history;
    history.synchronizeSession({ 1u, 1u });
    history.recordFiltered(23u, false, 0u, 50u, 1000u);
    history.synchronizeSession({});
    EXPECT_TRUE(history.isRetiredDuplicate(23u, false, 0u, 50u, false, 1010u));
    EXPECT_FALSE(history.isRetiredDuplicate(23u, false, 0u, 50u, true, 1010u));
    history.synchronizeSession({ 2u, 1u });
    history.recordFiltered(23u, false, 0u, 51u, 1010u);
    EXPECT_TRUE(history.isRetiredDuplicate(23u, false, 0u, 50u, false, 1020u));
    EXPECT_FALSE(history.isRetiredDuplicate(23u, false, 0u, 51u, false, 1020u));
    EXPECT_FALSE(history.isRetiredDuplicate(0u, false, 0u, 50u, false, 1020u));
}

TEST(X11FilteredKeyHistory, RetiredFrontierAdvancesWithinOneHostMillisecondAndAcrossNativeTimestampWrap){
    X11FilteredKeyHistory history;
    history.synchronizeSession({ 1u, 1u });
    history.recordFiltered(23u, false, 0xfffffff0u, 50u, 1000u);
    history.synchronizeSession({ 1u, 2u });
    history.recordFiltered(23u, false, 5u, 50u, 1000u);
    EXPECT_TRUE(history.isRetiredDuplicate(23u, false, 0xfffffff0u, 50u, false, 1010u));
    EXPECT_FALSE(history.isRetiredDuplicate(23u, false, 5u, 50u, false, 1010u));
    history.synchronizeSession({ 1u, 3u });
    EXPECT_TRUE(history.isRetiredDuplicate(23u, false, 5u, 50u, false, 1010u));
    EXPECT_FALSE(history.isRetiredDuplicate(23u, false, 6u, 50u, false, 1010u));
}

TEST(X11FilteredKeyHistory, ZeroTimeEchoKeepsTheXimSerialUpperBitsAcrossTheWireSequenceBoundary){
    X11FilteredKeyHistory history;
    history.synchronizeSession({ 1u, 1u });
    history.recordFiltered(23u, false, 0u, 0x0000fff0u, 1000u);
    history.synchronizeSession({ 1u, 2u });
    // XIM forwards low sequence bits in the event and high bits in its separate protocol field.
    constexpr u32 s_CurrentSerial = 0x00010010u;
    EXPECT_FALSE(history.isRetiredDuplicate(23u, false, 0u, s_CurrentSerial, false, 1010u));
    history.recordFiltered(23u, false, 0u, s_CurrentSerial, 1010u);
    EXPECT_TRUE(history.isForwardedDuplicate(23u, false, 0u, s_CurrentSerial, false, 1020u));
    EXPECT_FALSE(history.isRetiredDuplicate(23u, false, 0u, s_CurrentSerial, false, 1020u));
    EXPECT_TRUE(history.isRetiredDuplicate(23u, false, 0u, 0x0000fff0u, false, 1020u));
    history.synchronizeSession({ 1u, 3u });
    EXPECT_TRUE(history.isRetiredDuplicate(23u, false, 0u, s_CurrentSerial, false, 1030u));
    EXPECT_FALSE(history.isRetiredDuplicate(23u, false, 0u, 0x00020010u, false, 1030u));
}

TEST(X11FilteredKeyHistory, SessionChangesDoNotRefreshRetiredStampExpirationAndResetClearsBothHistories){
    X11FilteredKeyHistory history;
    constexpr u64 s_HalfRange = 1ull << 31u;
    history.synchronizeSession({ 1u, 1u });
    history.recordFiltered(23u, false, 100u, 50u, 1000u);
    history.synchronizeSession({ 1u, 2u });
    history.synchronizeSession({ 1u, 3u });
    EXPECT_TRUE(history.isRetiredDuplicate(23u, false, 100u, 50u, false, 1000u + s_HalfRange - 1u));
    EXPECT_FALSE(history.isRetiredDuplicate(23u, false, 100u, 50u, false, 1000u + s_HalfRange));
    history.reset();
    EXPECT_FALSE(history.isRetiredDuplicate(23u, false, 100u, 50u, false, 1001u));
    EXPECT_FALSE(history.isForwardedDuplicate(23u, false, 100u, 50u, false, 1001u));
}

TEST(X11PreeditBuffer, CharacterReplacementSplicesCompleteBmpAndSupplementaryScalars){
    NWB::Tests::TestArena arena;
    X11PreeditBuffer preedit(arena.arena);
    ASSERT_TRUE(preedit.replace(0u, 0u, "A\xED\x95\x9C\xF0\x9F\x98\x80Z", 3u));
    EXPECT_EQ(preedit.caretByte(), 8u);
    ASSERT_TRUE(preedit.replace(1u, 2u, "\xE2\x98\x83", 2u));
    EXPECT_EQ(preedit.text(), "A\xE2\x98\x83Z");
    EXPECT_EQ(preedit.caretByte(), 4u);
}

TEST(X11PreeditBuffer, SupplementaryInsertAndDeletionKeepScalarIndexedCaret){
    NWB::Tests::TestArena arena;
    X11PreeditBuffer preedit(arena.arena);
    ASSERT_TRUE(preedit.replace(0u, 0u, "\xED\x95\x9C", 1u));
    ASSERT_TRUE(preedit.replace(0u, 0u, "\xF0\x9F\x98\x80", 1u));
    EXPECT_EQ(preedit.caretByte(), 4u);
    ASSERT_TRUE(preedit.moveCaret(2u));
    EXPECT_EQ(preedit.caretByte(), 7u);
    ASSERT_TRUE(preedit.replace(0u, 1u, {}, 0u));
    EXPECT_EQ(preedit.text(), "\xED\x95\x9C");
    EXPECT_EQ(preedit.caretByte(), 0u);
    preedit.clear();
    EXPECT_TRUE(preedit.text().empty());
    EXPECT_EQ(preedit.caretByte(), 0u);
}

TEST(X11PreeditBuffer, LineEndUsesScalarCountAndUtf8ByteEnd){
    NWB::Tests::TestArena arena;
    X11PreeditBuffer preedit(arena.arena);
    ASSERT_TRUE(preedit.replace(0u, 0u, "A\xED\x95\x9C\xF0\x9F\x98\x80", 1u));
    EXPECT_EQ(preedit.moveCaretToEnd(), 3u);
    EXPECT_EQ(preedit.caretByte(), 8u);
    ASSERT_TRUE(preedit.moveCaret(0u));
    EXPECT_EQ(preedit.caretByte(), 0u);
    preedit.clear();
    EXPECT_EQ(preedit.moveCaretToEnd(), 0u);
}

TEST(X11PreeditBuffer, MalformedReplacementPreservesPublishedTextAndCaret){
    NWB::Tests::TestArena arena;
    X11PreeditBuffer preedit(arena.arena);
    ASSERT_TRUE(preedit.replace(0u, 0u, "valid", 3u));
    const Array<AStringView, 4u> invalid = { "\x80", "\xC0\xAF", "\xED\xA0\x80", AStringView("a\0b", 3u) };
    for(const AStringView text : invalid){
        EXPECT_FALSE(preedit.replace(1u, 2u, text, 1u));
        EXPECT_EQ(preedit.text(), "valid");
        EXPECT_EQ(preedit.caretByte(), 3u);
    }
}

TEST(X11PreeditBuffer, InvalidChangeOrCaretRangesAreFailureAtomic){
    NWB::Tests::TestArena arena;
    X11PreeditBuffer preedit(arena.arena);
    ASSERT_TRUE(preedit.replace(0u, 0u, "\xED\x95\x9C!", 1u));
    EXPECT_FALSE(preedit.replace(3u, 0u, "X", 0u));
    EXPECT_FALSE(preedit.replace(1u, 2u, "X", 0u));
    EXPECT_FALSE(preedit.replace(0u, 1u, "X", 3u));
    EXPECT_FALSE(preedit.moveCaret(3u));
    EXPECT_EQ(preedit.text(), "\xED\x95\x9C!");
    EXPECT_EQ(preedit.caretByte(), 3u);
}

TEST(X11PreeditBuffer, BoundedPreeditRejectsGrowthWithoutDiscardingText){
    NWB::Tests::TestArena arena;
    X11PreeditBuffer preedit(arena.arena);
    AString<Alloc::GlobalArena> maximum(X11PreeditBuffer::s_MaxBytes, 'x', arena.arena);
    ASSERT_TRUE(preedit.replace(0u, 0u, maximum, maximum.size()));
    EXPECT_FALSE(preedit.replace(0u, 0u, "x", 0u));
    EXPECT_EQ(preedit.text().size(), maximum.size());
    EXPECT_EQ(preedit.caretByte(), maximum.size());
    EXPECT_TRUE(preedit.replace(0u, 1u, "y", 1u));
}


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

