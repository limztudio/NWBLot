// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/os/linux/x11/text_input_preedit.h>
#include <core/os/linux/x11/text_input_dispatch.h>
#include <core/os/linux/wayland/text_input_state.h>
#include <core/os/text_input_text.h>
#include <tests/common/test_context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_linux_text_input_model_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core;

TEST(X11TextInputDispatchFence, CallbackCancellationDefersContextUntilOuterNativeCallReturns){
    X11TextInputDispatchFence fence;
    bool nativeContextAlive = true;
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

TEST(X11PreeditBuffer, CharacterReplacementProducesUtf8ByteCaret){
    NWB::Tests::TestArena arena;
    X11PreeditBuffer preedit(arena.arena);
    ASSERT_TRUE(preedit.replace(0u, 0u, "A\xED\x95\x9C\xF0\x9F\x98\x80Z", 3u));
    EXPECT_EQ(preedit.caretByte(), 8u);
    ASSERT_TRUE(preedit.replace(1u, 2u, "\xE2\x98\x83", 2u));
    EXPECT_EQ(preedit.text(), "A\xE2\x98\x83Z");
    EXPECT_EQ(preedit.caretByte(), 4u);
}

TEST(X11PreeditBuffer, InsertDeleteAndMoveRetainCharacterOffsets){
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
    const AStringView invalid[]{ "\x80", "\xC0\xAF", "\xED\xA0\x80", AStringView("a\0b", 3u) };
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

TEST(WaylandTextInputState, ShortSurroundingKeepsCompleteSelectionAndOffsets){
    const AStringView text = "A\xED\x95\x9C\xF0\x9F\x98\x80Z";
    const WaylandTextInputSurrounding slice = SliceWaylandTextInputSurrounding(text, 8u, 1u);
    ASSERT_TRUE(slice.available);
    EXPECT_EQ(slice.text, text);
    EXPECT_EQ(slice.offsetByte, 0u);
    EXPECT_EQ(slice.anchorByte, 8u);
    EXPECT_EQ(slice.caretByte, 1u);
}

TEST(WaylandTextInputState, LongSurroundingClipsWithoutSplittingUnicode){
    NWB::Tests::TestArena arena;
    AString<Alloc::GlobalArena> text(arena.arena);
    for(usize index = 0u; index < 3000u; ++index)
        text.append("\xED\x95\x9C");
    const WaylandTextInputSurrounding slice = SliceWaylandTextInputSurrounding(text, 4500u, 4503u);
    ASSERT_TRUE(slice.available);
    EXPECT_LE(slice.text.size(), 4000u);
    EXPECT_EQ(ValidateTextInputUtf8(slice.text, 4000u), TextInputAdmission::Accepted);
    EXPECT_EQ(slice.offsetByte + slice.anchorByte, 4500u);
    EXPECT_EQ(slice.offsetByte + slice.caretByte, 4503u);
    EXPECT_TRUE(IsTextInputUtf8Boundary(slice.text, slice.anchorByte));
    EXPECT_TRUE(IsTextInputUtf8Boundary(slice.text, slice.caretByte));
}

TEST(WaylandTextInputState, SelectionMustFitNativeMessageAndContainByteBoundaries){
    NWB::Tests::TestArena arena;
    AString<Alloc::GlobalArena> text(5000u, 'x', arena.arena);
    EXPECT_FALSE(SliceWaylandTextInputSurrounding(text, 0u, 4001u).available);
    EXPECT_FALSE(SliceWaylandTextInputSurrounding(text, 5001u, 1u).available);
    EXPECT_FALSE(SliceWaylandTextInputSurrounding("\xED\x95\x9C", 1u, 3u).available);
    const WaylandTextInputSurrounding exact = SliceWaylandTextInputSurrounding(text, 500u, 4500u);
    ASSERT_TRUE(exact.available);
    EXPECT_EQ(exact.text.size(), 4000u);
    EXPECT_EQ(exact.anchorByte, 0u);
    EXPECT_EQ(exact.caretByte, 4000u);
}

TEST(WaylandTextInputState, EdgeCursorSlicesUseAvailableContext){
    NWB::Tests::TestArena arena;
    AString<Alloc::GlobalArena> text(9000u, 'x', arena.arena);
    const WaylandTextInputSurrounding first = SliceWaylandTextInputSurrounding(text, 0u, 0u);
    ASSERT_TRUE(first.available);
    EXPECT_EQ(first.text.size(), 4000u);
    EXPECT_EQ(first.offsetByte, 0u);
    const WaylandTextInputSurrounding last = SliceWaylandTextInputSurrounding(text, 9000u, 9000u);
    ASSERT_TRUE(last.available);
    EXPECT_EQ(last.text.size(), 4000u);
    EXPECT_EQ(last.offsetByte, 5000u);
    EXPECT_EQ(last.caretByte, 4000u);
}

TEST(WaylandTextInputState, UnavailableSurroundingNeverRetagsOldNativeSnapshot){
    NWB::Tests::TestArena arena;
    WaylandTextInputSurroundingState wire;
    WaylandTextInputSerialTracker tracker;
    const TextInputSessionToken token{ 7u, 8u };
    ASSERT_TRUE(wire.update("old", 1u, 1u, 1u).available);
    tracker.record(1u, token, wire.revision());
    AString<Alloc::GlobalArena> large(5000u, 'x', arena.arena);
    EXPECT_FALSE(wire.update(large, 0u, 5000u, 2u).available);
    EXPECT_EQ(wire.revision(), 1u);
    tracker.record(2u, token, wire.revision());
    EXPECT_EQ(tracker.resolve(2u, token).revision, 1u);
    ASSERT_TRUE(wire.update("new", 3u, 3u, 3u).available);
    tracker.record(3u, token, wire.revision());
    EXPECT_EQ(tracker.resolve(3u, token).revision, 3u);
    wire.reset();
    EXPECT_FALSE(wire.update(large, 0u, 5000u, 4u).available);
    tracker.record(4u, token, wire.revision());
    EXPECT_FALSE(tracker.resolve(4u, token).revisionKnown);
}

TEST(WaylandTextInputState, NativeDeletionExcludesSelectionAndRequiresTheExactPublishedEndpoints){
    WaylandTextInputSurroundingState wire;
    ASSERT_TRUE(wire.update("abcDEFghi", 3u, 6u, 7u).available);
    const WaylandTextInputProvenance known{ { 1u, 2u }, 7u, true };
    EXPECT_TRUE(CanApplyWaylandTextInputDeletion(known, 7u, wire, 3u, 6u, 2u, 1u));
    EXPECT_TRUE(CanApplyWaylandTextInputDeletion(known, 7u, wire, 3u, 6u, 3u, 3u));
    EXPECT_TRUE(CanApplyWaylandTextInputDeletion(known, 7u, wire, 3u, 6u, 0u, 0u));
    EXPECT_FALSE(CanApplyWaylandTextInputDeletion(known, 7u, wire, 3u, 6u, 4u, 0u));
    EXPECT_FALSE(CanApplyWaylandTextInputDeletion(known, 7u, wire, 3u, 6u, 0u, 4u));
    EXPECT_FALSE(CanApplyWaylandTextInputDeletion(known, 7u, wire, 1u, 3u, 2u, 1u));
    EXPECT_FALSE(CanApplyWaylandTextInputDeletion(known, 8u, wire, 3u, 6u, 2u, 1u));
    EXPECT_FALSE(CanApplyWaylandTextInputDeletion({ { 1u, 2u }, 0u, false }, 7u, wire, 3u, 6u, 2u, 1u));
    ASSERT_TRUE(wire.update("abcDEFghi", 6u, 3u, 8u).available);
    const WaylandTextInputProvenance reverse{ { 1u, 2u }, 8u, true };
    EXPECT_TRUE(CanApplyWaylandTextInputDeletion(reverse, 8u, wire, 6u, 3u, 2u, 1u));
    EXPECT_FALSE(CanApplyWaylandTextInputDeletion(reverse, 8u, wire, 3u, 6u, 2u, 1u));
}

TEST(WaylandTextInputState, DeletionCannotReachBeyondTheSentSliceEvenWhenTheFullTextContainsThoseBytes){
    NWB::Tests::TestArena arena;
    AString<Alloc::GlobalArena> text(9000u, 'x', arena.arena);
    WaylandTextInputSurroundingState wire;
    const WaylandTextInputSurrounding sent = wire.update(text, 4500u, 4503u, 7u);
    ASSERT_TRUE(sent.available);
    ASSERT_GT(sent.offsetByte, 0u);
    const usize before = Min(sent.anchorByte, sent.caretByte);
    const usize after = sent.text.size() - Max(sent.anchorByte, sent.caretByte);
    ASSERT_LT(before + 1u, 4500u);
    ASSERT_LT(after + 1u, text.size() - 4503u);
    const WaylandTextInputProvenance known{ { 1u, 2u }, 7u, true };
    EXPECT_TRUE(CanApplyWaylandTextInputDeletion(known, 7u, wire, 4500u, 4503u, before, after));
    EXPECT_FALSE(CanApplyWaylandTextInputDeletion(known, 7u, wire, 4500u, 4503u, before + 1u, after));
    EXPECT_FALSE(CanApplyWaylandTextInputDeletion(known, 7u, wire, 4500u, 4503u, before, after + 1u));
    EXPECT_FALSE(CanApplyWaylandTextInputDeletion(known, 7u, wire, 4500u, 4503u, Limit<usize>::s_Max, 0u));
    EXPECT_FALSE(CanApplyWaylandTextInputDeletion(known, 7u, wire, 4500u, 4503u, 0u, Limit<usize>::s_Max));
}

TEST(WaylandTextInputState, WireSelectionEndpointsMustMatchDirectionOffsetAndCurrentPositions){
    NWB::Tests::TestArena arena;
    AString<Alloc::GlobalArena> text(9000u, 'x', arena.arena);
    WaylandTextInputSurroundingState wire;
    ASSERT_TRUE(wire.update(text, 4500u, 4503u, 7u).available);
    const WaylandTextInputProvenance known{ { 1u, 2u }, 7u, true };
    EXPECT_TRUE(CanApplyWaylandTextInputDeletion(known, 7u, wire, 4500u, 4503u, 2u, 1u));
    EXPECT_FALSE(CanApplyWaylandTextInputDeletion(known, 7u, wire, 4501u, 4503u, 2u, 1u));
    EXPECT_FALSE(CanApplyWaylandTextInputDeletion(known, 7u, wire, 4500u, 4504u, 2u, 1u));
    EXPECT_FALSE(CanApplyWaylandTextInputDeletion(known, 7u, wire, 4503u, 4500u, 2u, 1u));
    EXPECT_FALSE(CanApplyWaylandTextInputDeletion(known, 7u, wire, 0u, 4503u, 0u, 0u));
    EXPECT_FALSE(CanApplyWaylandTextInputDeletion(known, 7u, wire, 4500u, 0u, 0u, 0u));
    EXPECT_FALSE(CanApplyWaylandTextInputDeletion(known, 7u, wire, Limit<usize>::s_Max, 4503u, 0u, 0u));
}

TEST(WaylandTextInputState, WireEdgeCursorsAndASelectionFillingTheMessageHaveExactOuterLimits){
    NWB::Tests::TestArena arena;
    AString<Alloc::GlobalArena> text(9000u, 'x', arena.arena);
    WaylandTextInputSurroundingState wire;
    const WaylandTextInputProvenance known{ { 1u, 2u }, 7u, true };
    ASSERT_TRUE(wire.update(text, 0u, 0u, 7u).available);
    EXPECT_TRUE(CanApplyWaylandTextInputDeletion(known, 7u, wire, 0u, 0u, 0u, 4000u));
    EXPECT_FALSE(CanApplyWaylandTextInputDeletion(known, 7u, wire, 0u, 0u, 1u, 0u));
    EXPECT_FALSE(CanApplyWaylandTextInputDeletion(known, 7u, wire, 0u, 0u, 0u, 4001u));
    ASSERT_TRUE(wire.update(text, 9000u, 9000u, 7u).available);
    EXPECT_TRUE(CanApplyWaylandTextInputDeletion(known, 7u, wire, 9000u, 9000u, 4000u, 0u));
    EXPECT_FALSE(CanApplyWaylandTextInputDeletion(known, 7u, wire, 9000u, 9000u, 4001u, 0u));
    EXPECT_FALSE(CanApplyWaylandTextInputDeletion(known, 7u, wire, 9000u, 9000u, 0u, 1u));
    ASSERT_TRUE(wire.update(text, 500u, 4500u, 7u).available);
    EXPECT_TRUE(CanApplyWaylandTextInputDeletion(known, 7u, wire, 500u, 4500u, 0u, 0u));
    EXPECT_FALSE(CanApplyWaylandTextInputDeletion(known, 7u, wire, 500u, 4500u, 1u, 0u));
    EXPECT_FALSE(CanApplyWaylandTextInputDeletion(known, 7u, wire, 500u, 4500u, 0u, 1u));
}

TEST(WaylandTextInputState, UnavailableReplacementPreservesEveryPreviouslySentNumericBound){
    NWB::Tests::TestArena arena;
    AString<Alloc::GlobalArena> text(9000u, 'x', arena.arena);
    WaylandTextInputSurroundingState wire;
    const WaylandTextInputSurrounding sent = wire.update(text, 4500u, 4503u, 7u);
    ASSERT_TRUE(sent.available);
    const usize before = Min(sent.anchorByte, sent.caretByte);
    const usize after = sent.text.size() - Max(sent.anchorByte, sent.caretByte);
    const WaylandTextInputProvenance known{ { 1u, 2u }, 7u, true };
    EXPECT_FALSE(wire.update(text, 0u, 5000u, 8u).available);
    EXPECT_EQ(wire.revision(), 7u);
    EXPECT_TRUE(CanApplyWaylandTextInputDeletion(known, 7u, wire, 4500u, 4503u, before, after));
    EXPECT_FALSE(CanApplyWaylandTextInputDeletion(known, 7u, wire, 4500u, 4503u, before + 1u, after));
    EXPECT_FALSE(CanApplyWaylandTextInputDeletion(known, 8u, wire, 4500u, 4503u, 0u, 0u));
    EXPECT_FALSE(CanApplyWaylandTextInputDeletion({ { 1u, 2u }, 8u, true }, 8u, wire, 4500u, 4503u, 0u, 0u));
    wire.reset();
    EXPECT_EQ(wire.revision(), 0u);
    EXPECT_FALSE(CanApplyWaylandTextInputDeletion(known, 7u, wire, 4500u, 4503u, 0u, 0u));
    EXPECT_FALSE(wire.update(text, 0u, 5000u, 9u).available);
    EXPECT_EQ(wire.revision(), 0u);
}

TEST(WaylandTextInputState, WireBoundsOwnNoViewOfTheCallerText){
    NWB::Tests::TestArena arena;
    AString<Alloc::GlobalArena> text(9000u, 'x', arena.arena);
    WaylandTextInputSurroundingState wire;
    const WaylandTextInputSurrounding sent = wire.update(text, 4500u, 4503u, 7u);
    ASSERT_TRUE(sent.available);
    const usize before = Min(sent.anchorByte, sent.caretByte);
    const usize after = sent.text.size() - Max(sent.anchorByte, sent.caretByte);
    text.clear();
    text.shrink_to_fit();
    const WaylandTextInputProvenance known{ { 1u, 2u }, 7u, true };
    EXPECT_TRUE(CanApplyWaylandTextInputDeletion(known, 7u, wire, 4500u, 4503u, before, after));
    EXPECT_FALSE(CanApplyWaylandTextInputDeletion(known, 7u, wire, 4500u, 4503u, before + 1u, after));
}

TEST(WaylandTextInputState, EmptyOrResetWireStillRequiresKnownNonzeroSnapshotProvenance){
    WaylandTextInputSurroundingState wire;
    const WaylandTextInputProvenance known{ { 1u, 2u }, 7u, true };
    EXPECT_FALSE(CanApplyWaylandTextInputDeletion(known, 7u, wire, 0u, 0u, 0u, 0u));
    ASSERT_TRUE(wire.update({}, 0u, 0u, 7u).available);
    EXPECT_TRUE(CanApplyWaylandTextInputDeletion(known, 7u, wire, 0u, 0u, 0u, 0u));
    EXPECT_FALSE(CanApplyWaylandTextInputDeletion(known, 7u, wire, 0u, 0u, 1u, 0u));
    EXPECT_FALSE(CanApplyWaylandTextInputDeletion(known, 7u, wire, 0u, 0u, 0u, 1u));
    EXPECT_FALSE(CanApplyWaylandTextInputDeletion({ {}, 7u, true }, 7u, wire, 0u, 0u, 0u, 0u));
    EXPECT_FALSE(CanApplyWaylandTextInputDeletion({ { 1u, 2u }, 7u, false }, 7u, wire, 0u, 0u, 0u, 0u));
    ASSERT_TRUE(wire.update({}, 0u, 0u, 0u).available);
    EXPECT_FALSE(CanApplyWaylandTextInputDeletion({ { 1u, 2u }, 0u, true }, 0u, wire, 0u, 0u, 0u, 0u));
}

TEST(WaylandTextInputState, DeferredSurroundingChangesCannotRetagUnsentSliceOrCurrentSerials){
    WaylandTextInputSurroundingState wire;
    WaylandTextInputSerialTracker tracker;
    WaylandTextInputDeferredState deferred;
    const TextInputSessionToken token{ 7u, 8u };
    ASSERT_TRUE(wire.update("abcDEFghi", 3u, 6u, 7u).available);
    tracker.record(42u, token, wire.revision());
    deferred.surroundingChanged(TextInputChangeCause::Other);
    deferred.caretChanged();
    ASSERT_TRUE(deferred.pending());
    const WaylandTextInputProvenance old = tracker.resolve(42u, token);
    EXPECT_FALSE(CanApplyWaylandTextInputDeletion(old, 8u, wire, 3u, 6u, 2u, 1u));
    EXPECT_FALSE(CanApplyWaylandTextInputDeletion({ token, 8u, true }, 8u, wire, 3u, 6u, 2u, 1u));
    deferred.clear();
    ASSERT_TRUE(wire.update("aDEFhi", 1u, 4u, 8u).available);
    tracker.record(43u, token, wire.revision());
    EXPECT_TRUE(CanApplyWaylandTextInputDeletion(tracker.resolve(43u, token), 8u, wire, 1u, 4u, 1u, 2u));
    EXPECT_FALSE(CanApplyWaylandTextInputDeletion(old, 8u, wire, 1u, 4u, 1u, 2u));
}

TEST(WaylandTextInputState, DelayedSerialWithTheCurrentWireRevisionStillAdmitsDeletion){
    WaylandTextInputSurroundingState wire;
    WaylandTextInputSerialTracker tracker;
    const TextInputSessionToken token{ 7u, 8u };
    ASSERT_TRUE(wire.update("abcDEFghi", 3u, 6u, 7u).available);
    tracker.record(42u, token, wire.revision());
    tracker.record(43u, token, wire.revision());
    EXPECT_TRUE(CanApplyWaylandTextInputDeletion(tracker.resolve(42u, token), 7u, wire, 3u, 6u, 2u, 1u));
    EXPECT_TRUE(CanApplyWaylandTextInputDeletion(tracker.resolve(43u, token), 7u, wire, 3u, 6u, 2u, 1u));
}

TEST(WaylandTextInputState, NegativeCaretEdgesRoundOutwardWithoutWideningExactEdges){
    const TextInputRect exact = WaylandTextInputRectForPixels({ -6, -8, 2, 4 }, 2);
    EXPECT_EQ(exact.x, -3);
    EXPECT_EQ(exact.y, -4);
    EXPECT_EQ(exact.width, 1);
    EXPECT_EQ(exact.height, 2);
    const TextInputRect crossed = WaylandTextInputRectForPixels({ -3, -3, 4, 4 }, 2);
    EXPECT_EQ(crossed.x, -2);
    EXPECT_EQ(crossed.y, -2);
    EXPECT_EQ(crossed.width, 3);
    EXPECT_EQ(crossed.height, 3);
}

TEST(WaylandTextInputState, PositiveCaretEdgesAndUnitScaleRemainConsistent){
    const TextInputRect scaled = WaylandTextInputRectForPixels({ 3, 5, 4, 4 }, 2);
    EXPECT_EQ(scaled.x, 1);
    EXPECT_EQ(scaled.y, 2);
    EXPECT_EQ(scaled.width, 3);
    EXPECT_EQ(scaled.height, 3);
    const TextInputRect unit = WaylandTextInputRectForPixels({ -7, 5, 2, 3 }, 0);
    EXPECT_EQ(unit.x, -7);
    EXPECT_EQ(unit.y, 5);
    EXPECT_EQ(unit.width, 2);
    EXPECT_EQ(unit.height, 3);
}

TEST(WaylandTextInputState, DeferredCaretPreservesLatestSurroundingChangeCause){
    WaylandTextInputDeferredState state;
    state.surroundingChanged(TextInputChangeCause::InputMethod);
    state.caretChanged();
    EXPECT_TRUE(state.pending());
    EXPECT_EQ(state.cause(), TextInputChangeCause::InputMethod);
    state.surroundingChanged(TextInputChangeCause::Other);
    state.caretChanged();
    EXPECT_EQ(state.cause(), TextInputChangeCause::Other);
    state.surroundingChanged(TextInputChangeCause::InputMethod);
    EXPECT_EQ(state.cause(), TextInputChangeCause::InputMethod);
    state.clear();
    EXPECT_FALSE(state.pending());
    state.caretChanged();
    EXPECT_EQ(state.cause(), TextInputChangeCause::InputMethod);
}

TEST(WaylandTextInputState, SerialTracksHistoricalCurrentSessionRevision){
    WaylandTextInputSerialTracker tracker;
    const TextInputSessionToken token{ 7u, 9u };
    tracker.record(10u, token, 1u);
    tracker.record(11u, token, 2u);
    const WaylandTextInputProvenance old = tracker.resolve(10u, token);
    EXPECT_EQ(old.token, token);
    EXPECT_TRUE(old.revisionKnown);
    EXPECT_EQ(old.revision, 1u);
    EXPECT_EQ(tracker.resolve(11u, token).revision, 2u);
    EXPECT_FALSE(tracker.resolve(9u, token).token.valid());
    EXPECT_FALSE(tracker.resolve(12u, token).token.valid());
}

TEST(WaylandTextInputState, NewSessionAndDisableFenceDelayedOldSerials){
    WaylandTextInputSerialTracker tracker;
    const TextInputSessionToken first{ 1u, 1u };
    const TextInputSessionToken second{ 1u, 2u };
    tracker.record(20u, first, 1u);
    tracker.record(22u, second, 1u);
    EXPECT_FALSE(tracker.resolve(20u, second).token.valid());
    EXPECT_FALSE(tracker.resolve(22u, first).token.valid());
    EXPECT_EQ(tracker.resolve(22u, second).token, second);
    tracker.record(23u, {}, 0u);
    EXPECT_FALSE(tracker.resolve(22u, second).token.valid());
}

TEST(WaylandTextInputState, SerialWraparoundPreservesCurrentSessionInterval){
    WaylandTextInputSerialTracker tracker;
    const TextInputSessionToken token{ 2u, 3u };
    tracker.record(0xFFFFFFFEu, token, 1u);
    tracker.record(0xFFFFFFFFu, token, 2u);
    tracker.record(0u, token, 3u);
    tracker.record(1u, token, 4u);
    EXPECT_EQ(tracker.resolve(0xFFFFFFFFu, token).revision, 2u);
    EXPECT_EQ(tracker.resolve(0u, token).revision, 3u);
    EXPECT_EQ(tracker.resolve(1u, token).revision, 4u);
    EXPECT_FALSE(tracker.resolve(0xFFFFFFFDu, token).token.valid());
    EXPECT_FALSE(tracker.resolve(2u, token).token.valid());
    WaylandTextInputSurroundingState wire;
    ASSERT_TRUE(wire.update("abcDEFghi", 3u, 6u, 4u).available);
    EXPECT_TRUE(CanApplyWaylandTextInputDeletion(tracker.resolve(1u, token), 4u, wire, 3u, 6u, 2u, 1u));
    EXPECT_FALSE(CanApplyWaylandTextInputDeletion(tracker.resolve(0u, token), 4u, wire, 3u, 6u, 2u, 1u));
}

TEST(WaylandTextInputState, RingEvictionStillAdmitsDelayedCommitButMarksDeletionRevisionUnknown){
    WaylandTextInputSerialTracker tracker;
    const TextInputSessionToken token{ 3u, 5u };
    for(u32 serial = 100u; serial < 200u; ++serial)
        tracker.record(serial, token, serial - 99u);
    const WaylandTextInputProvenance old = tracker.resolve(100u, token);
    EXPECT_EQ(old.token, token);
    EXPECT_FALSE(old.revisionKnown);
    EXPECT_EQ(tracker.resolve(199u, token).revision, 100u);
    EXPECT_FALSE(tracker.resolve(99u, token).token.valid());
    WaylandTextInputSurroundingState wire;
    ASSERT_TRUE(wire.update("abcDEFghi", 3u, 6u, 100u).available);
    EXPECT_FALSE(CanApplyWaylandTextInputDeletion(old, 100u, wire, 3u, 6u, 2u, 1u));
    EXPECT_TRUE(CanApplyWaylandTextInputDeletion(tracker.resolve(199u, token), 100u, wire, 3u, 6u, 2u, 1u));
    tracker.reset();
    EXPECT_FALSE(tracker.resolve(199u, token).token.valid());
    EXPECT_FALSE(CanApplyWaylandTextInputDeletion(tracker.resolve(199u, token), 100u, wire, 3u, 6u, 2u, 1u));
}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

