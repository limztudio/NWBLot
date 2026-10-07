// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/os/text_input_service.h>
#include <tests/common/test_context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_text_input_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core;

class FakeTextInputService final : public QueuedTextInputService{
public:
    explicit FakeTextInputService(Alloc::GlobalArena& arena)
        : QueuedTextInputService(arena)
        , m_nativeSurrounding(arena)
    {}


public:
    using QueuedTextInputService::cancelSession;
    using QueuedTextInputService::emitCommit;
    using QueuedTextInputService::emitPreedit;
    using QueuedTextInputService::emitDeleteSurrounding;
    using QueuedTextInputService::surroundingText;
    using QueuedTextInputService::surroundingCaretByte;
    using QueuedTextInputService::surroundingAnchorByte;
    using QueuedTextInputService::caretRect;

    [[nodiscard]] virtual TextInputCapabilities capabilities()const noexcept override{
        return m_supported ? TextInputCapabilities{ true, true, true, true } : TextInputCapabilities{};
    }


protected:
    [[nodiscard]] virtual TextInputAdmission::Enum startNativeSession(
        const TextInputSessionToken token,
        const TextInputSessionDesc& desc
    )override{
        m_nativeToken = token;
        m_nativeSurrounding.assign(desc.surrounding.data(), desc.surrounding.size());
        if(m_onStart)
            m_onStart();
        return m_startAdmission;
    }

    virtual void endNativeSession(const TextInputSessionToken token)override{
        EXPECT_FALSE(activeSession().valid());
        m_nativeToken = {};
        ++m_endCount;
        if(m_onEnd)
            m_onEnd(token);
    }

    [[nodiscard]] virtual TextInputAdmission::Enum updateNativeCaret(const TextInputRect)override{
        if(m_onCaret)
            m_onCaret();
        return TextInputAdmission::Accepted;
    }

    virtual void updateNativeSurrounding(
        const AStringView text, usize, usize, u64, const TextInputChangeCause::Enum cause
    )override{
        m_nativeSurrounding.assign(text.data(), text.size());
        m_nativeCause = cause;
        if(m_onSurrounding)
            m_onSurrounding();
    }


public:
    AString<Alloc::GlobalArena> m_nativeSurrounding;
    Function<void()> m_onStart;
    Function<void()> m_onCaret;
    Function<void()> m_onSurrounding;
    Function<void(TextInputSessionToken)> m_onEnd;
    TextInputSessionToken m_nativeToken;
    TextInputAdmission::Enum m_startAdmission = TextInputAdmission::Accepted;
    usize m_endCount = 0u;
    TextInputChangeCause::Enum m_nativeCause = TextInputChangeCause::Other;
    bool m_supported = true;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(TextInput, BeginRequiresFocusCapabilityAndOnlyOneActiveSession){
    NWB::Tests::TestArena arena;
    FakeTextInputService service(arena.arena);
    EXPECT_EQ(service.begin({}).admission, TextInputAdmission::Unavailable);
    ASSERT_TRUE(service.setFocused(true));
    service.m_supported = false;
    EXPECT_EQ(service.begin({}).admission, TextInputAdmission::Unsupported);
    service.m_supported = true;
    const TextInputBeginResult first = service.begin({});
    ASSERT_EQ(first.admission, TextInputAdmission::Accepted);
    ASSERT_TRUE(first.token.valid());
    EXPECT_EQ(service.activeSession(), first.token);
    EXPECT_EQ(service.begin({}).admission, TextInputAdmission::Busy);
    ASSERT_TRUE(service.end(first.token));
    EXPECT_FALSE(service.activeSession().valid());
}

TEST(TextInput, InputsAndPolledEventsOwnCopiedUtf8Bytes){
    NWB::Tests::TestArena arena;
    FakeTextInputService service(arena.arena);
    ASSERT_TRUE(service.setFocused(true));
    AString<Alloc::GlobalArena> surrounding("A\xED\x95\x9C\xF0\x9F\x98\x80", arena.arena);
    const TextInputSessionToken token = service.begin({ {}, surrounding, 1u, 4u }).token;
    ASSERT_TRUE(token.valid());
    surrounding.assign("changed caller input");
    EXPECT_EQ(service.surroundingText(), "A\xED\x95\x9C\xF0\x9F\x98\x80");
    EXPECT_EQ(service.m_nativeSurrounding, service.surroundingText());
    AString<Alloc::GlobalArena> input("\xED\x95\x9C\xF0\x9F\x98\x80", arena.arena);
    ASSERT_EQ(service.emitCommit(token, input), TextInputAdmission::Accepted);
    input.assign("changed event input");
    TextInputEvent event(arena.arena);
    ASSERT_EQ(service.poll(token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.text, "\xED\x95\x9C\xF0\x9F\x98\x80");
    EXPECT_EQ(event.token, token);
    EXPECT_EQ(event.kind, TextInputEventKind::Commit);
    ASSERT_EQ(service.emitCommit(token, "next"), TextInputAdmission::Accepted);
    EXPECT_EQ(event.text, "\xED\x95\x9C\xF0\x9F\x98\x80");
}

TEST(TextInput, EndRejectsLateEventsForeignTokensAndReusedGenerations){
    NWB::Tests::TestArena arena;
    FakeTextInputService service(arena.arena);
    FakeTextInputService foreign(arena.arena);
    ASSERT_TRUE(service.setFocused(true));
    ASSERT_TRUE(foreign.setFocused(true));
    const TextInputSessionToken old = service.begin({}).token;
    const TextInputSessionToken other = foreign.begin({}).token;
    ASSERT_EQ(service.emitCommit(old, "queued"), TextInputAdmission::Accepted);
    ASSERT_TRUE(service.end(old));
    const TextInputSessionToken current = service.begin({}).token;
    ASSERT_TRUE(current.valid());
    EXPECT_NE(old.generation, current.generation);
    EXPECT_NE(other.service, current.service);
    TextInputEvent event(arena.arena);
    EXPECT_EQ(service.poll(old, event), TextInputPollResult::InvalidSession);
    EXPECT_EQ(service.poll(other, event), TextInputPollResult::InvalidSession);
    EXPECT_EQ(service.emitCommit(old, "late"), TextInputAdmission::InvalidSession);
    EXPECT_EQ(service.emitCommit(other, "foreign"), TextInputAdmission::InvalidSession);
    EXPECT_FALSE(service.end(old));
    EXPECT_EQ(service.poll(current, event), TextInputPollResult::Pending);
}

TEST(TextInput, FocusLossReplacesQueuedWorkWithOneTerminalCancellation){
    NWB::Tests::TestArena arena;
    FakeTextInputService service(arena.arena);
    ASSERT_TRUE(service.setFocused(true));
    const TextInputSessionToken token = service.begin({}).token;
    ASSERT_EQ(service.emitCommit(token, "unconsumed"), TextInputAdmission::Accepted);
    ASSERT_EQ(service.emitPreedit(token, "draft", 5u, 5u), TextInputAdmission::Accepted);
    ASSERT_TRUE(service.setFocused(false));
    EXPECT_FALSE(service.activeSession().valid());
    EXPECT_EQ(service.m_endCount, 1u);
    EXPECT_EQ(service.emitCommit(token, "stale"), TextInputAdmission::InvalidSession);
    TextInputEvent event(arena.arena);
    ASSERT_EQ(service.poll(token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.kind, TextInputEventKind::Cancelled);
    EXPECT_EQ(event.cancelReason, TextInputCancelReason::FocusLost);
    EXPECT_TRUE(event.text.empty());
    EXPECT_EQ(service.poll(token, event), TextInputPollResult::InvalidSession);
    EXPECT_EQ(service.begin({}).admission, TextInputAdmission::Unavailable);
    ASSERT_TRUE(service.setFocused(true));
    EXPECT_TRUE(service.begin({}).token.valid());
}

TEST(TextInput, NewSessionInvalidatesUnpolledCancellation){
    NWB::Tests::TestArena arena;
    FakeTextInputService service(arena.arena);
    ASSERT_TRUE(service.setFocused(true));
    const TextInputSessionToken old = service.begin({}).token;
    ASSERT_TRUE(service.cancelSession(old, TextInputCancelReason::NativeCancelled));
    const TextInputSessionToken current = service.begin({}).token;
    TextInputEvent event(arena.arena);
    EXPECT_EQ(service.poll(old, event), TextInputPollResult::InvalidSession);
    EXPECT_EQ(service.poll(current, event), TextInputPollResult::Pending);
}

TEST(TextInput, ConsecutivePreeditCoalescesWithoutCrossingCommitOrDeletion){
    NWB::Tests::TestArena arena;
    FakeTextInputService service(arena.arena);
    ASSERT_TRUE(service.setFocused(true));
    const TextInputSessionToken token = service.begin({ {}, "abc", 1u, 1u }).token;
    const u64 revision = service.surroundingRevision(token);
    ASSERT_EQ(service.emitPreedit(token, "draft", 5u, 5u), TextInputAdmission::Accepted);
    ASSERT_EQ(service.emitPreedit(token, "final draft", 11u, 11u, false), TextInputAdmission::Accepted);
    ASSERT_EQ(service.emitCommit(token, "committed"), TextInputAdmission::Accepted);
    ASSERT_EQ(service.emitPreedit(token, "new", 3u, 3u), TextInputAdmission::Accepted);
    ASSERT_EQ(service.emitDeleteSurrounding(token, 1u, 1u, revision, TextInputDeletionBasis::Caret), TextInputAdmission::Accepted);
    ASSERT_EQ(service.emitPreedit(token, "last", 4u, 4u), TextInputAdmission::Accepted);
    TextInputEvent event(arena.arena);
    ASSERT_EQ(service.poll(token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.text, "final draft");
    EXPECT_FALSE(event.caretVisible);
    u64 lastSequence = event.sequence;
    ASSERT_EQ(service.poll(token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.kind, TextInputEventKind::Commit);
    EXPECT_EQ(event.text, "committed");
    EXPECT_GT(event.sequence, lastSequence);
    lastSequence = event.sequence;
    ASSERT_EQ(service.poll(token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.text, "new");
    EXPECT_GT(event.sequence, lastSequence);
    ASSERT_EQ(service.poll(token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.kind, TextInputEventKind::DeleteSurrounding);
    EXPECT_EQ(event.deleteBeforeBytes, 1u);
    EXPECT_EQ(event.deleteAfterBytes, 1u);
    ASSERT_EQ(service.poll(token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.text, "last");
    EXPECT_EQ(service.poll(token, event), TextInputPollResult::Pending);
}

TEST(TextInput, QueueOverflowCancelsInsteadOfApplyingPartialCommittedText){
    NWB::Tests::TestArena arena;
    FakeTextInputService service(arena.arena);
    ASSERT_TRUE(service.setFocused(true));
    const TextInputSessionToken token = service.begin({}).token;
    for(usize index = 0u; index < s_TextInputMaxEvents; ++index)
        ASSERT_EQ(service.emitCommit(token, "x"), TextInputAdmission::Accepted);
    EXPECT_EQ(service.emitCommit(token, "overflow"), TextInputAdmission::QueueFull);
    EXPECT_FALSE(service.activeSession().valid());
    TextInputEvent event(arena.arena);
    ASSERT_EQ(service.poll(token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.kind, TextInputEventKind::Cancelled);
    EXPECT_EQ(event.cancelReason, TextInputCancelReason::Overflow);
    EXPECT_EQ(service.poll(token, event), TextInputPollResult::InvalidSession);
}

TEST(TextInput, QueuedByteBudgetCancelsEvenBelowEventCountLimit){
    NWB::Tests::TestArena arena;
    FakeTextInputService service(arena.arena);
    ASSERT_TRUE(service.setFocused(true));
    const TextInputSessionToken token = service.begin({}).token;
    AString<Alloc::GlobalArena> text(arena.arena);
    text.assign(s_TextInputMaxEventTextBytes, 'x');
    for(usize index = 0u; index < s_TextInputMaxQueuedTextBytes / text.size(); ++index)
        ASSERT_EQ(service.emitCommit(token, text), TextInputAdmission::Accepted);
    EXPECT_EQ(service.emitCommit(token, "x"), TextInputAdmission::QueueFull);
    TextInputEvent event(arena.arena);
    ASSERT_EQ(service.poll(token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.cancelReason, TextInputCancelReason::Overflow);
}

TEST(TextInput, InvalidUpdatesAreAtomicAndDeleteOffsetsRequireUtf8BoundariesAndRevision){
    NWB::Tests::TestArena arena;
    FakeTextInputService service(arena.arena);
    ASSERT_TRUE(service.setFocused(true));
    const TextInputSessionToken token = service.begin({ {}, "A\xED\x95\x9C" "B", 4u, 4u }).token;
    ASSERT_TRUE(token.valid());
    EXPECT_EQ(service.updateSurrounding(token, "bad\xF0\x9F", 0u, 0u), TextInputAdmission::InvalidText);
    EXPECT_EQ(service.updateSurrounding(token, "\xED\x95\x9C", 1u, 3u), TextInputAdmission::InvalidRange);
    EXPECT_EQ(service.surroundingText(), "A\xED\x95\x9C" "B");
    EXPECT_EQ(service.surroundingCaretByte(), 4u);
    EXPECT_EQ(service.surroundingRevision(token), 1u);
    const u64 revision = service.surroundingRevision(token);
    EXPECT_EQ(service.updateCaret(token, { 0, 0, 0, 1 }), TextInputAdmission::InvalidRange);
    EXPECT_EQ(service.caretRect().width, 1);
    EXPECT_EQ(service.emitDeleteSurrounding(token, 1u, 0u, revision, TextInputDeletionBasis::Caret), TextInputAdmission::InvalidRange);
    EXPECT_EQ(service.emitDeleteSurrounding(token, 5u, 0u, revision, TextInputDeletionBasis::Caret), TextInputAdmission::InvalidRange);
    EXPECT_EQ(service.emitDeleteSurrounding(token, 0u, 2u, revision, TextInputDeletionBasis::Caret), TextInputAdmission::InvalidRange);
    ASSERT_EQ(service.emitDeleteSurrounding(token, 3u, 1u, revision, TextInputDeletionBasis::Caret), TextInputAdmission::Accepted);
    TextInputEvent event(arena.arena);
    ASSERT_EQ(service.poll(token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.surroundingRevision, 1u);
    ASSERT_EQ(service.updateSurrounding(token, "updated", 0u, 0u), TextInputAdmission::Accepted);
    EXPECT_EQ(service.surroundingRevision(token), 2u);
    const u64 currentRevision = service.surroundingRevision(token);
    EXPECT_EQ(service.emitDeleteSurrounding(token, 0u, 1u, revision, TextInputDeletionBasis::Caret), TextInputAdmission::InvalidRange);
    EXPECT_EQ(service.emitDeleteSurrounding(token, 0u, 1u, currentRevision, TextInputDeletionBasis::Caret), TextInputAdmission::Accepted);
}

TEST(TextInput, MalformedNativeTextAndPreeditSelectionsCannotEnterQueue){
    NWB::Tests::TestArena arena;
    FakeTextInputService service(arena.arena);
    ASSERT_TRUE(service.setFocused(true));
    const TextInputSessionToken token = service.begin({}).token;
    EXPECT_EQ(service.emitCommit(token, AStringView("a\0b", 3u)), TextInputAdmission::InvalidText);
    EXPECT_EQ(service.emitPreedit(token, "\xED\x95\x9C", 1u, 3u), TextInputAdmission::InvalidRange);
    AString<Alloc::GlobalArena> large(arena.arena);
    large.assign(s_TextInputMaxEventTextBytes + 1u, 'x');
    EXPECT_EQ(service.emitCommit(token, large), TextInputAdmission::TooLarge);
    EXPECT_EQ(service.activeSession(), token);
    TextInputEvent event(arena.arena);
    EXPECT_EQ(service.poll(token, event), TextInputPollResult::Pending);
}

TEST(TextInput, CancellationInvalidatesBeforeNativeCleanupAndRejectsReentry){
    NWB::Tests::TestArena arena;
    FakeTextInputService service(arena.arena);
    ASSERT_TRUE(service.setFocused(true));
    const TextInputSessionToken token = service.begin({}).token;
    service.m_onEnd = [&](const TextInputSessionToken ended){
        EXPECT_EQ(ended, token);
        EXPECT_EQ(service.emitCommit(ended, "reentrant late"), TextInputAdmission::InvalidSession);
        EXPECT_EQ(service.begin({}).admission, TextInputAdmission::Busy);
        EXPECT_FALSE(service.end(ended));
    };
    ASSERT_TRUE(service.cancelSession(token, TextInputCancelReason::NativeCancelled));
    EXPECT_EQ(service.m_endCount, 1u);
    service.m_onEnd = {};
    ASSERT_TRUE(service.begin({}).token.valid());
}

TEST(TextInput, NativeStartFailureRetiresTokenAndAnyEarlyEvent){
    NWB::Tests::TestArena arena;
    FakeTextInputService service(arena.arena);
    ASSERT_TRUE(service.setFocused(true));
    TextInputSessionToken failed;
    service.m_onStart = [&](){
        failed = service.activeSession();
        EXPECT_EQ(service.emitCommit(failed, "early event"), TextInputAdmission::Accepted);
    };
    service.m_startAdmission = TextInputAdmission::NativeFailure;
    EXPECT_EQ(service.begin({}).admission, TextInputAdmission::NativeFailure);
    EXPECT_FALSE(service.activeSession().valid());
    TextInputEvent event(arena.arena);
    EXPECT_EQ(service.poll(failed, event), TextInputPollResult::InvalidSession);
    EXPECT_EQ(service.m_endCount, 1u);
}

TEST(TextInput, WrongThreadCannotMutateOrPollOwnerSession){
    NWB::Tests::TestArena arena;
    FakeTextInputService service(arena.arena);
    ASSERT_TRUE(service.setFocused(true));
    const TextInputSessionToken token = service.begin({}).token;
    Thread worker([&](){
        TextInputEvent event(arena.arena);
        EXPECT_FALSE(service.isOwnerThread());
        EXPECT_FALSE(service.activeSession().valid());
        EXPECT_EQ(service.surroundingRevision(token), 0u);
        EXPECT_EQ(service.begin({}).admission, TextInputAdmission::WrongThread);
        EXPECT_EQ(service.emitCommit(token, "wrong thread"), TextInputAdmission::WrongThread);
        EXPECT_EQ(service.poll(token, event), TextInputPollResult::WrongThread);
        EXPECT_EQ(service.updateCaret(token, {}), TextInputAdmission::WrongThread);
        EXPECT_EQ(service.updateSurrounding(token, {}, 0u, 0u), TextInputAdmission::WrongThread);
        EXPECT_FALSE(service.setFocused(false));
        EXPECT_FALSE(service.end(token));
    });
    worker.join();
    EXPECT_EQ(service.activeSession(), token);
    TextInputEvent event(arena.arena);
    EXPECT_EQ(service.poll(token, event), TextInputPollResult::Pending);
}

TEST(TextInput, NullNativeFactoryReportsUnsupportedWithoutClaimingTextInput){
    NWB::Tests::TestArena arena;
    const GlobalUniquePtr<ITextInputService> service = CreateTextInputService(arena.arena, nullptr);
    ASSERT_NE(service.get(), nullptr);
    EXPECT_FALSE(service->capabilities().commit);
    EXPECT_FALSE(service->capabilities().preedit);
    EXPECT_FALSE(service->capabilities().surrounding);
    EXPECT_FALSE(service->capabilities().deleteSurrounding);
    ASSERT_TRUE(service->setFocused(true));
    EXPECT_EQ(service->begin({}).admission, TextInputAdmission::Unsupported);
}

TEST(TextInput, ReentrantNativeCaretCancellationReturnsFailureAndRetainsTerminalEvent){
    NWB::Tests::TestArena arena;
    FakeTextInputService service(arena.arena);
    ASSERT_TRUE(service.setFocused(true));
    const TextInputSessionToken token = service.begin({}).token;
    service.m_onCaret = [&](){
        EXPECT_TRUE(service.cancelSession(token, TextInputCancelReason::NativeFailure));
    };
    EXPECT_EQ(service.updateCaret(token, { 20, 30, 2, 18 }), TextInputAdmission::NativeFailure);
    EXPECT_FALSE(service.activeSession().valid());
    TextInputEvent event(arena.arena);
    ASSERT_EQ(service.poll(token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.kind, TextInputEventKind::Cancelled);
    EXPECT_EQ(event.cancelReason, TextInputCancelReason::NativeFailure);
}

TEST(TextInput, ReentrantNativeSurroundingCancellationReturnsFailureAndRetainsTerminalEvent){
    NWB::Tests::TestArena arena;
    FakeTextInputService service(arena.arena);
    ASSERT_TRUE(service.setFocused(true));
    const TextInputSessionToken token = service.begin({}).token;
    service.m_onSurrounding = [&](){
        EXPECT_TRUE(service.cancelSession(token, TextInputCancelReason::NativeFailure));
    };
    EXPECT_EQ(service.updateSurrounding(token, "replacement", 2u, 4u), TextInputAdmission::NativeFailure);
    EXPECT_FALSE(service.activeSession().valid());
    EXPECT_EQ(service.surroundingRevision(token), 0u);
    TextInputEvent event(arena.arena);
    ASSERT_EQ(service.poll(token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.kind, TextInputEventKind::Cancelled);
    EXPECT_EQ(event.cancelReason, TextInputCancelReason::NativeFailure);
}


TEST(TextInput, UnknownSurroundingChangeCausePreservesTextRevisionAndNativeState){
    NWB::Tests::TestArena arena;
    FakeTextInputService service(arena.arena);
    ASSERT_TRUE(service.setFocused(true));
    const TextInputSessionToken token = service.begin({}).token;
    ASSERT_EQ(service.updateSurrounding(token, "local change", 0u, 12u), TextInputAdmission::Accepted);
    const u64 revision = service.surroundingRevision(token);
    EXPECT_EQ(
        service.updateSurrounding(token, "invalid", 0u, 0u, static_cast<TextInputChangeCause::Enum>(255u)),
        TextInputAdmission::InvalidRange
    );
    EXPECT_EQ(service.surroundingText(), "local change");
    EXPECT_EQ(service.surroundingRevision(token), revision);
    EXPECT_EQ(service.m_nativeCause, TextInputChangeCause::Other);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

