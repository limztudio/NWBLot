// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/os/text_input_service.h>

#include <tests/common/test_context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_text_input_selection_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core;

class SelectionTextInput final : public QueuedTextInputService{
public:
    using QueuedTextInputService::emitCommit;
    using QueuedTextInputService::emitPreedit;
    using QueuedTextInputService::emitDeleteSurrounding;


public:
    explicit SelectionTextInput(Alloc::GlobalArena& arena)
        : QueuedTextInputService(arena)
    {}


public:
    [[nodiscard]] virtual TextInputCapabilities capabilities()const noexcept override{ return { true, true, true, true }; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(TextInputSelection, ReverseSelectionHasTheSameOuterByteLimits){
    NWB::Tests::TestArena arena;
    SelectionTextInput service(arena.arena);
    ASSERT_TRUE(service.setFocused(true));
    const auto begun = service.begin({ {}, "abcDEFghi", 6u, 3u });
    ASSERT_TRUE(begun);
    const u64 revision = service.surroundingRevision(*begun);
    EXPECT_EQ(service.emitDeleteSurrounding(*begun, 4u, 0u, revision, TextInputDeletionBasis::Selection), TextInputAdmission::InvalidRange);
    EXPECT_EQ(service.emitDeleteSurrounding(*begun, 0u, 4u, revision, TextInputDeletionBasis::Selection), TextInputAdmission::InvalidRange);
    ASSERT_EQ(service.emitDeleteSurrounding(*begun, 3u, 3u, revision, TextInputDeletionBasis::Selection), TextInputAdmission::Accepted);
    TextInputEvent event(arena.arena);
    ASSERT_EQ(service.poll(*begun, event), TextInputPollResult::Event);
    EXPECT_EQ(event.deleteBeforeBytes, 3u);
    EXPECT_EQ(event.deleteAfterBytes, 3u);
    EXPECT_EQ(event.deletionBasis, TextInputDeletionBasis::Selection);
    EXPECT_EQ(service.poll(*begun, event), TextInputPollResult::Pending);
}

TEST(TextInputSelection, UnknownBasisAndMissingOrStaleRevisionDoNotQueueAnyEvent){
    NWB::Tests::TestArena arena;
    SelectionTextInput service(arena.arena);
    ASSERT_TRUE(service.setFocused(true));
    const auto begun = service.begin({ {}, "abcDEFghi", 3u, 6u });
    ASSERT_TRUE(begun);
    const u64 revision = service.surroundingRevision(*begun);
    ASSERT_EQ(service.updateSurrounding(*begun, "abcDEFghi", 3u, 6u), TextInputAdmission::Accepted);
    const u64 currentRevision = service.surroundingRevision(*begun);
    EXPECT_EQ(service.emitDeleteSurrounding(*begun, 2u, 1u, revision, TextInputDeletionBasis::Selection), TextInputAdmission::InvalidRange);
    EXPECT_EQ(service.emitDeleteSurrounding(*begun, 2u, 1u, 0u, TextInputDeletionBasis::Selection), TextInputAdmission::InvalidRange);
    constexpr TextInputDeletionBasis::Enum s_InvalidBasis = static_cast<TextInputDeletionBasis::Enum>(255u);
    EXPECT_EQ(service.emitDeleteSurrounding(*begun, 0u, 0u, currentRevision, s_InvalidBasis), TextInputAdmission::InvalidRange);
    TextInputEvent event(arena.arena);
    EXPECT_EQ(service.poll(*begun, event), TextInputPollResult::Pending);
    EXPECT_EQ(service.activeSession(), *begun);
    ASSERT_EQ(service.emitDeleteSurrounding(*begun, 2u, 1u, currentRevision, TextInputDeletionBasis::Selection), TextInputAdmission::Accepted);
    ASSERT_EQ(service.poll(*begun, event), TextInputPollResult::Event);
    EXPECT_EQ(event.surroundingRevision, currentRevision);
    EXPECT_EQ(event.deletionBasis, TextInputDeletionBasis::Selection);
    EXPECT_EQ(event.deleteBeforeBytes, 2u);
    EXPECT_EQ(event.deleteAfterBytes, 1u);
    EXPECT_EQ(service.poll(*begun, event), TextInputPollResult::Pending);
}

TEST(TextInputSelection, OuterUtf8EndpointsMustRemainScalarBoundaries){
    NWB::Tests::TestArena arena;
    SelectionTextInput service(arena.arena);
    ASSERT_TRUE(service.setFocused(true));
    const auto begun = service.begin({ {}, "a한DEF글z", 4u, 7u });
    ASSERT_TRUE(begun);
    const u64 revision = service.surroundingRevision(*begun);
    EXPECT_EQ(service.emitDeleteSurrounding(*begun, 1u, 0u, revision, TextInputDeletionBasis::Selection), TextInputAdmission::InvalidRange);
    EXPECT_EQ(service.emitDeleteSurrounding(*begun, 0u, 1u, revision, TextInputDeletionBasis::Selection), TextInputAdmission::InvalidRange);
    ASSERT_EQ(service.emitDeleteSurrounding(*begun, 3u, 3u, revision, TextInputDeletionBasis::Selection), TextInputAdmission::Accepted);
    TextInputEvent event(arena.arena);
    ASSERT_EQ(service.poll(*begun, event), TextInputPollResult::Event);
    EXPECT_EQ(event.deleteBeforeBytes, 3u);
    EXPECT_EQ(event.deleteAfterBytes, 3u);
}

TEST(TextInputSelection, PollCopiesTheBasisAndNonDeletionEventsResetAReusedOutput){
    NWB::Tests::TestArena outputArena;
    TextInputEvent event(outputArena.arena);
    TextInputSessionToken token;
    {
        NWB::Tests::TestArena nativeArena;
        SelectionTextInput service(nativeArena.arena);
        ASSERT_TRUE(service.setFocused(true));
        const auto begun = service.begin({ {}, "abcDEFghi", 3u, 6u });
        ASSERT_TRUE(begun);
        const u64 revision = service.surroundingRevision(*begun);
        token = *begun;
        ASSERT_EQ(service.emitDeleteSurrounding(token, 2u, 1u, revision, TextInputDeletionBasis::Selection), TextInputAdmission::Accepted);
        ASSERT_EQ(service.poll(token, event), TextInputPollResult::Event);
        EXPECT_EQ(event.deletionBasis, TextInputDeletionBasis::Selection);
        ASSERT_EQ(service.emitCommit(token, "X"), TextInputAdmission::Accepted);
        ASSERT_EQ(service.poll(token, event), TextInputPollResult::Event);
        EXPECT_EQ(event.deletionBasis, TextInputDeletionBasis::Caret);
        ASSERT_EQ(service.emitDeleteSurrounding(token, 0u, 0u, revision, TextInputDeletionBasis::Selection), TextInputAdmission::Accepted);
        ASSERT_EQ(service.poll(token, event), TextInputPollResult::Event);
        ASSERT_TRUE(service.end(token));
    }
    EXPECT_EQ(event.token, token);
    EXPECT_EQ(event.kind, TextInputEventKind::DeleteSurrounding);
    EXPECT_EQ(event.deletionBasis, TextInputDeletionBasis::Selection);
    EXPECT_EQ(event.deleteBeforeBytes, 0u);
    EXPECT_EQ(event.deleteAfterBytes, 0u);
}

TEST(TextInputSelection, DeletionPreservesThePreeditCommitOrderingBarrier){
    NWB::Tests::TestArena arena;
    SelectionTextInput service(arena.arena);
    ASSERT_TRUE(service.setFocused(true));
    const auto begun = service.begin({ {}, "abcDEFghi", 3u, 6u });
    ASSERT_TRUE(begun);
    const u64 revision = service.surroundingRevision(*begun);
    ASSERT_EQ(service.emitPreedit(*begun, {}, 0u, 0u), TextInputAdmission::Accepted);
    ASSERT_EQ(service.emitDeleteSurrounding(*begun, 2u, 1u, revision, TextInputDeletionBasis::Selection), TextInputAdmission::Accepted);
    ASSERT_EQ(service.emitCommit(*begun, "X"), TextInputAdmission::Accepted);
    ASSERT_EQ(service.emitPreedit(*begun, "next", 0u, 4u), TextInputAdmission::Accepted);
    const Array<TextInputEventKind::Enum, 4u> kinds{ TextInputEventKind::Preedit, TextInputEventKind::DeleteSurrounding, TextInputEventKind::Commit, TextInputEventKind::Preedit };
    TextInputEvent event(arena.arena);
    u64 previousSequence = 0u;
    for(const auto kind : kinds){
        ASSERT_EQ(service.poll(*begun, event), TextInputPollResult::Event);
        EXPECT_EQ(event.kind, kind);
        EXPECT_GT(event.sequence, previousSequence);
        EXPECT_EQ(event.deletionBasis, kind == TextInputEventKind::DeleteSurrounding ? TextInputDeletionBasis::Selection : TextInputDeletionBasis::Caret);
        previousSequence = event.sequence;
    }
    EXPECT_EQ(service.poll(*begun, event), TextInputPollResult::Pending);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/os/clipboard_service.h>

#include <tests/common/test_context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_clipboard_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core;

class FakeClipboardService final : public QueuedClipboardService{
public:
    explicit FakeClipboardService(Alloc::GlobalArena& arena)
        : QueuedClipboardService(arena)
        , m_text(arena)
    {}


public:
    [[nodiscard]] virtual ClipboardCapabilities capabilities(const ClipboardChannel::Enum channel)const noexcept override{
        return channel == ClipboardChannel::Clipboard ? ClipboardCapabilities{ true, true } : ClipboardCapabilities{};
    }


protected:
    virtual void startNativeRequest(const ClipboardRequestToken token, const ClipboardOperation::Enum operation, const ClipboardChannel::Enum, const AStringView input)override{
        m_nativeToken = token;
        if(operation == ClipboardOperation::ReadText){
            ++m_readCount;
            if(m_onRead)
                m_onRead();
            if(m_nativeToken != token)
                return;
            m_nativeToken = {};
            if(!completeNativeRequest(token, m_readStatus, m_text))
                NWB_FATAL_ASSERT(false);
        }
        else{
            ++m_writeCount;
            m_text.assign(input.data(), input.size());
            m_nativeToken = {};
            if(!completeNativeRequest(token, ClipboardStatus::Success))
                NWB_FATAL_ASSERT(false);
        }
    }

    virtual void cancelNativeRequest(const ClipboardRequestToken token)noexcept override{
        if(m_nativeToken == token)
            m_nativeToken = {};
    }


public:
    AString<Alloc::GlobalArena> m_text;
    Function<void()> m_onRead;
    ClipboardStatus::Enum m_readStatus = ClipboardStatus::Success;
    usize m_readCount = 0u;
    usize m_writeCount = 0u;


private:
    ClipboardRequestToken m_nativeToken;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Clipboard, RequestsCopyTextAndPublishOnlyWhenPumped){
    NWB::Tests::TestArena arena;
    FakeClipboardService service(arena.arena);
    ClipboardCompletion completion(arena.arena);
    AString<Alloc::GlobalArena> input("Hello \xED\x95\x9C\xEA\xB8\x80 \xF0\x9F\x98\x80", arena.arena);
    const AString<Alloc::GlobalArena> original = input;
    const auto write = service.requestWriteText(ClipboardChannel::Clipboard, input);
    ASSERT_TRUE(write);
    input.assign("changed");
    EXPECT_EQ(service.poll(*write, completion), ClipboardPollResult::Pending);
    EXPECT_EQ(service.m_writeCount, 0u);
    const auto read = service.requestReadText(ClipboardChannel::Clipboard);
    ASSERT_TRUE(read);
    ASSERT_TRUE(service.pump());
    EXPECT_EQ(service.poll(*write, completion), ClipboardPollResult::Completed);
    EXPECT_EQ(completion.status, ClipboardStatus::Success);
    EXPECT_EQ(service.poll(*read, completion), ClipboardPollResult::Completed);
    EXPECT_EQ(completion.text, original);
    EXPECT_EQ(completion.token, *read);
    EXPECT_EQ(service.poll(*read, completion), ClipboardPollResult::InvalidRequest);
}

TEST(Clipboard, CancelPreventsPendingWritesAndRejectsReusedGenerations){
    NWB::Tests::TestArena arena;
    FakeClipboardService service(arena.arena);
    ClipboardCompletion completion(arena.arena);
    const auto canceledAdmission = service.requestWriteText(ClipboardChannel::Clipboard, "discard");
    ASSERT_TRUE(canceledAdmission);
    const ClipboardRequestToken canceled = *canceledAdmission;
    ASSERT_TRUE(service.cancel(canceled));
    const auto currentAdmission = service.requestWriteText(ClipboardChannel::Clipboard, "keep");
    ASSERT_TRUE(currentAdmission);
    const ClipboardRequestToken current = *currentAdmission;
    EXPECT_NE(canceled.generation, current.generation);
    EXPECT_EQ(service.poll(canceled, completion), ClipboardPollResult::InvalidRequest);
    EXPECT_FALSE(service.cancel(canceled));
    ASSERT_TRUE(service.pump());
    EXPECT_EQ(service.m_writeCount, 1u);
    EXPECT_EQ(service.m_text, "keep");
    ASSERT_TRUE(service.cancel(current));
    EXPECT_EQ(service.poll(current, completion), ClipboardPollResult::InvalidRequest);
    EXPECT_EQ(service.m_text, "keep");
}

TEST(Clipboard, ReusedSlotsKeepRequestOrdering){
    NWB::Tests::TestArena arena;
    FakeClipboardService service(arena.arena);
    ClipboardCompletion completion(arena.arena);
    const auto firstAdmission = service.requestReadText(ClipboardChannel::Clipboard);
    ASSERT_TRUE(firstAdmission);
    const ClipboardRequestToken first = *firstAdmission;
    const auto secondAdmission = service.requestWriteText(ClipboardChannel::Clipboard, "second");
    ASSERT_TRUE(secondAdmission);
    const ClipboardRequestToken second = *secondAdmission;
    ASSERT_TRUE(service.cancel(first));
    const auto thirdAdmission = service.requestReadText(ClipboardChannel::Clipboard);
    ASSERT_TRUE(thirdAdmission);
    const ClipboardRequestToken third = *thirdAdmission;
    ASSERT_TRUE(service.pump());
    EXPECT_EQ(service.poll(second, completion), ClipboardPollResult::Completed);
    EXPECT_EQ(service.poll(third, completion), ClipboardPollResult::Completed);
    EXPECT_EQ(completion.text, "second");
}

TEST(Clipboard, TokensCannotCrossServicesOrSurviveServiceRecreation){
    NWB::Tests::TestArena arena;
    ClipboardCompletion completion(arena.arena);
    ClipboardRequestToken old;
    {
        FakeClipboardService previous(arena.arena);
        const auto oldAdmission = previous.requestReadText(ClipboardChannel::Clipboard);
        ASSERT_TRUE(oldAdmission);
        old = *oldAdmission;
    }
    FakeClipboardService service(arena.arena);
    const auto currentAdmission = service.requestReadText(ClipboardChannel::Clipboard);
    ASSERT_TRUE(currentAdmission);
    const ClipboardRequestToken current = *currentAdmission;
    EXPECT_NE(old.service, current.service);
    EXPECT_EQ(service.poll(old, completion), ClipboardPollResult::InvalidRequest);
    EXPECT_FALSE(service.cancel(old));
    EXPECT_EQ(service.poll({}, completion), ClipboardPollResult::InvalidRequest);
}

TEST(Clipboard, CanceledNativeCompletionCannotReachNewRequest){
    NWB::Tests::TestArena arena;
    FakeClipboardService service(arena.arena);
    ClipboardCompletion completion(arena.arena);
    const auto oldAdmission = service.requestReadText(ClipboardChannel::Clipboard);
    ASSERT_TRUE(oldAdmission);
    const ClipboardRequestToken old = *oldAdmission;
    ClipboardRequestToken current;
    service.m_onRead = [&](){
        EXPECT_TRUE(service.cancel(old));
        const auto currentAdmission = service.requestReadText(ClipboardChannel::Clipboard);
        ASSERT_TRUE(currentAdmission);
        current = *currentAdmission;
        EXPECT_FALSE(service.pump());
    };
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(service.pump()));
    EXPECT_EQ(service.poll(old, completion), ClipboardPollResult::InvalidRequest);
    EXPECT_EQ(service.poll(current, completion), ClipboardPollResult::Pending);
    service.m_onRead = {};
    service.m_text.assign("new completion");
    ASSERT_TRUE(service.pump());
    EXPECT_EQ(service.poll(current, completion), ClipboardPollResult::Completed);
    EXPECT_EQ(completion.text, "new completion");
}

TEST(Clipboard, UnsupportedChannelsAndUnavailableReadsStayExplicit){
    NWB::Tests::TestArena arena;
    FakeClipboardService service(arena.arena);
    ClipboardCompletion completion(arena.arena);
    EXPECT_FALSE(service.capabilities(ClipboardChannel::PrimarySelection).readText);
    const auto primaryAdmission = service.requestReadText(ClipboardChannel::PrimarySelection);
    ASSERT_TRUE(primaryAdmission);
    const ClipboardRequestToken primary = *primaryAdmission;
    ASSERT_TRUE(service.pump());
    EXPECT_EQ(service.poll(primary, completion), ClipboardPollResult::Completed);
    EXPECT_EQ(completion.status, ClipboardStatus::Unsupported);
    EXPECT_EQ(service.m_readCount, 0u);
    service.m_readStatus = ClipboardStatus::Unavailable;
    service.m_text.assign("must not escape failed read");
    const auto unavailableAdmission = service.requestReadText(ClipboardChannel::Clipboard);
    ASSERT_TRUE(unavailableAdmission);
    const ClipboardRequestToken unavailable = *unavailableAdmission;
    ASSERT_TRUE(service.pump());
    EXPECT_EQ(service.poll(unavailable, completion), ClipboardPollResult::Completed);
    EXPECT_EQ(completion.status, ClipboardStatus::Unavailable);
    EXPECT_TRUE(completion.text.empty());
}

TEST(Clipboard, UnsupportedFactoryDoesNotAccessNativeClipboard){
    NWB::Tests::TestArena arena;
    auto serviceResult = CreateClipboardService(arena.arena, nullptr);
    ASSERT_TRUE(serviceResult);
    GlobalUniquePtr<IClipboardService> service = Move(*serviceResult);
    ASSERT_TRUE(service);
    EXPECT_FALSE(service->capabilities(ClipboardChannel::Clipboard).readText);
    EXPECT_FALSE(service->capabilities(ClipboardChannel::Clipboard).writeText);
    EXPECT_FALSE(service->capabilities(ClipboardChannel::PrimarySelection).readText);
    const auto tokenAdmission = service->requestWriteText(ClipboardChannel::Clipboard, "unused");
    ASSERT_TRUE(tokenAdmission);
    const ClipboardRequestToken token = *tokenAdmission;
    ASSERT_TRUE(service->pump());
    ClipboardCompletion completion(arena.arena);
    EXPECT_EQ(service->poll(token, completion), ClipboardPollResult::Completed);
    EXPECT_EQ(completion.status, ClipboardStatus::Unsupported);
}

TEST(Clipboard, AdmissionBoundsOutstandingRequestsAndText){
    NWB::Tests::TestArena arena;
    FakeClipboardService service(arena.arena);
    Array<ClipboardRequestToken, s_ClipboardMaxOutstandingRequests> tokens;
    for(ClipboardRequestToken& token : tokens){
        const auto request = service.requestReadText(ClipboardChannel::Clipboard);
        ASSERT_TRUE(request);
        token = *request;
    }
    const auto queueFullRead = service.requestReadText(ClipboardChannel::Clipboard);
    ASSERT_FALSE(queueFullRead);
    EXPECT_EQ(queueFullRead.error(), ClipboardAdmission::QueueFull);
    for(const ClipboardRequestToken token : tokens)
        ASSERT_TRUE(service.cancel(token));
    const auto invalidAdmission = service.requestWriteText(ClipboardChannel::Clipboard, AStringView("a\0b", 3u));
    ASSERT_TRUE(invalidAdmission);
    const ClipboardRequestToken invalid = *invalidAdmission;
    ClipboardCompletion completion(arena.arena);
    EXPECT_EQ(service.poll(invalid, completion), ClipboardPollResult::Completed);
    EXPECT_EQ(completion.status, ClipboardStatus::InvalidText);
    AString<Alloc::GlobalArena> oversized(s_ClipboardMaxTextBytes + 1u, 'x', arena.arena);
    const auto largeAdmission = service.requestWriteText(ClipboardChannel::Clipboard, oversized);
    ASSERT_TRUE(largeAdmission);
    const ClipboardRequestToken large = *largeAdmission;
    EXPECT_EQ(service.poll(large, completion), ClipboardPollResult::Completed);
    EXPECT_EQ(completion.status, ClipboardStatus::TooLarge);
    EXPECT_EQ(service.m_writeCount, 0u);
}

TEST(Clipboard, WrongThreadCallsNeverTouchRequestStorage){
    NWB::Tests::TestArena arena;
    FakeClipboardService service(arena.arena);
    ClipboardCompletion completion(arena.arena);
    const auto tokenAdmission = service.requestReadText(ClipboardChannel::Clipboard);
    ASSERT_TRUE(tokenAdmission);
    const ClipboardRequestToken token = *tokenAdmission;
    Thread worker([&](){
        EXPECT_FALSE(service.isOwnerThread());
        const auto wrongThreadRead = service.requestReadText(ClipboardChannel::Clipboard);
        ASSERT_FALSE(wrongThreadRead);
        EXPECT_EQ(wrongThreadRead.error(), ClipboardAdmission::WrongThread);
        const auto wrongThreadWrite = service.requestWriteText(ClipboardChannel::Clipboard, "ignored");
        ASSERT_FALSE(wrongThreadWrite);
        EXPECT_EQ(wrongThreadWrite.error(), ClipboardAdmission::WrongThread);
        EXPECT_EQ(service.poll(token, completion), ClipboardPollResult::WrongThread);
        EXPECT_FALSE(service.cancel(token));
        EXPECT_FALSE(service.pump());
    });
    worker.join();
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    EXPECT_EQ(service.poll(token, completion), ClipboardPollResult::Pending);
    ASSERT_TRUE(service.pump());
    EXPECT_EQ(service.poll(token, completion), ClipboardPollResult::Completed);
}

TEST(Clipboard, ConsumedCompletionOutlivesServiceAndItsArena){
    NWB::Tests::TestArena outputArena;
    ClipboardCompletion completion(outputArena.arena);
    {
        NWB::Tests::TestArena serviceArena;
        FakeClipboardService service(serviceArena.arena);
        service.m_text.assign("owned result");
        const auto tokenAdmission = service.requestReadText(ClipboardChannel::Clipboard);
        ASSERT_TRUE(tokenAdmission);
        const ClipboardRequestToken token = *tokenAdmission;
        ASSERT_TRUE(service.pump());
        EXPECT_EQ(service.poll(token, completion), ClipboardPollResult::Completed);
    }
    EXPECT_EQ(completion.text, "owned result");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/os/clipboard_service.h>

#include <tests/common/test_context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_clipboard_async_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core;

class DelayedClipboardService final : public QueuedClipboardService{
public:
    explicit DelayedClipboardService(Alloc::GlobalArena& arena)
        : QueuedClipboardService(arena)
        , m_startedText(arena)
    {}


public:
    [[nodiscard]] virtual ClipboardCapabilities capabilities(const ClipboardChannel::Enum channel)const noexcept override{
        return channel == ClipboardChannel::Clipboard ? ClipboardCapabilities{ true, true } : ClipboardCapabilities{};
    }

    [[nodiscard]] b8 deliver(const ClipboardRequestToken token, const ClipboardStatus::Enum status, const AStringView text = {}){
        const b8 completed = completeNativeRequest(token, status, text);
        if(completed && token == m_nativeToken)
            m_nativeToken = {};
        return completed;
    }


protected:
    virtual void startNativeRequest(const ClipboardRequestToken token, const ClipboardOperation::Enum operation, [[maybe_unused]] const ClipboardChannel::Enum channel, const AStringView text)override{
        m_nativeToken = token;
        m_nativeOperation = operation;
        m_startedText.assign(text.data(), text.size());
        ++m_startCount;
        if(m_onStart)
            m_onStart();
    }

    virtual void cancelNativeRequest(const ClipboardRequestToken token)override{
        ++m_cancelCount;
        if(m_nativeToken == token)
            m_nativeToken = {};
        if(m_onCancel)
            m_onCancel();
    }


public:
    AString<Alloc::GlobalArena> m_startedText;
    Function<void()> m_onStart;
    Function<void()> m_onCancel;
    ClipboardRequestToken m_nativeToken;
    ClipboardOperation::Enum m_nativeOperation = ClipboardOperation::ReadText;
    usize m_startCount = 0u;
    usize m_cancelCount = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(ClipboardAsync, PendingNativeRequestPreservesFifoAndCopiedWriteInput){
    NWB::Tests::TestArena arena;
    DelayedClipboardService service(arena.arena);
    ClipboardCompletion completion(arena.arena);
    const auto firstAdmission = service.requestReadText(ClipboardChannel::Clipboard);
    ASSERT_TRUE(firstAdmission);
    const ClipboardRequestToken first = *firstAdmission;
    AString<Alloc::GlobalArena> input("copied write", arena.arena);
    const auto secondAdmission = service.requestWriteText(ClipboardChannel::Clipboard, input);
    ASSERT_TRUE(secondAdmission);
    const ClipboardRequestToken second = *secondAdmission;
    const auto thirdAdmission = service.requestReadText(ClipboardChannel::Clipboard);
    ASSERT_TRUE(thirdAdmission);
    const ClipboardRequestToken third = *thirdAdmission;
    input.assign("changed caller input");

    ASSERT_TRUE(service.pump());
    EXPECT_EQ(service.m_nativeToken, first);
    ASSERT_TRUE(service.pump());
    EXPECT_EQ(service.m_startCount, 1u);
    EXPECT_EQ(service.poll(second, completion), ClipboardPollResult::Pending);
    ASSERT_TRUE(service.deliver(first, ClipboardStatus::Success, "first result"));
    ASSERT_TRUE(service.pump());
    EXPECT_EQ(service.m_nativeToken, second);
    EXPECT_EQ(service.m_nativeOperation, ClipboardOperation::WriteText);
    EXPECT_EQ(service.m_startedText, "copied write");
    ASSERT_TRUE(service.deliver(second, ClipboardStatus::Success));
    ASSERT_TRUE(service.pump());
    EXPECT_EQ(service.m_nativeToken, third);
    EXPECT_EQ(service.m_startCount, 3u);
    ASSERT_TRUE(service.deliver(third, ClipboardStatus::Success, "third result"));

    ASSERT_EQ(service.poll(first, completion), ClipboardPollResult::Completed);
    EXPECT_EQ(completion.text, "first result");
    ASSERT_EQ(service.poll(second, completion), ClipboardPollResult::Completed);
    EXPECT_EQ(completion.operation, ClipboardOperation::WriteText);
    EXPECT_TRUE(completion.text.empty());
    ASSERT_EQ(service.poll(third, completion), ClipboardPollResult::Completed);
    EXPECT_EQ(completion.text, "third result");
}

TEST(ClipboardAsync, CancellationReleasesFifoAndLateNativeRepliesCannotCompleteReplacement){
    NWB::Tests::TestArena arena;
    DelayedClipboardService service(arena.arena);
    ClipboardCompletion completion(arena.arena);
    const auto oldAdmission = service.requestReadText(ClipboardChannel::Clipboard);
    ASSERT_TRUE(oldAdmission);
    const ClipboardRequestToken old = *oldAdmission;
    const auto nextAdmission = service.requestWriteText(ClipboardChannel::Clipboard, "next");
    ASSERT_TRUE(nextAdmission);
    const ClipboardRequestToken next = *nextAdmission;
    ASSERT_TRUE(service.pump());
    ASSERT_TRUE(service.cancel(old));
    EXPECT_EQ(service.m_cancelCount, 1u);
    const auto replacementAdmission = service.requestReadText(ClipboardChannel::Clipboard);
    ASSERT_TRUE(replacementAdmission);
    const ClipboardRequestToken replacement = *replacementAdmission;
    ASSERT_TRUE(service.pump());
    EXPECT_EQ(service.m_nativeToken, next);
    EXPECT_FALSE(service.deliver(old, ClipboardStatus::Success, "stale bytes"));
    EXPECT_EQ(service.poll(old, completion), ClipboardPollResult::InvalidRequest);
    ASSERT_TRUE(service.deliver(next, ClipboardStatus::Success));
    ASSERT_TRUE(service.pump());
    EXPECT_EQ(service.m_nativeToken, replacement);
    EXPECT_FALSE(service.deliver(old, ClipboardStatus::Success, "stale bytes"));
    EXPECT_EQ(service.poll(replacement, completion), ClipboardPollResult::Pending);
    ASSERT_TRUE(service.deliver(replacement, ClipboardStatus::Success, "replacement bytes"));
    ASSERT_EQ(service.poll(replacement, completion), ClipboardPollResult::Completed);
    EXPECT_EQ(completion.text, "replacement bytes");
}

TEST(ClipboardAsync, CancellationInvalidatesTokenBeforeReentrantNativeCleanup){
    NWB::Tests::TestArena arena;
    DelayedClipboardService service(arena.arena);
    ClipboardCompletion completion(arena.arena);
    const auto oldAdmission = service.requestReadText(ClipboardChannel::Clipboard);
    ASSERT_TRUE(oldAdmission);
    const ClipboardRequestToken old = *oldAdmission;
    ASSERT_TRUE(service.pump());
    ClipboardRequestToken replacement;
    b8 observedCancellation = false;
    service.m_onCancel = [&](){
        if(observedCancellation)
            return;
        observedCancellation = true;
        EXPECT_FALSE(service.cancel(old));
        EXPECT_EQ(service.poll(old, completion), ClipboardPollResult::InvalidRequest);
        EXPECT_FALSE(service.deliver(old, ClipboardStatus::Success, "late during cancellation"));
        const auto replacementAdmission = service.requestReadText(ClipboardChannel::Clipboard);
        ASSERT_TRUE(replacementAdmission);
        replacement = *replacementAdmission;
    };

    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(service.cancel(old)));
    EXPECT_TRUE(observedCancellation);
    EXPECT_EQ(service.m_cancelCount, 1u);
    ASSERT_TRUE(replacement.valid());
    EXPECT_EQ(service.poll(replacement, completion), ClipboardPollResult::Pending);
    service.m_onCancel = {};
    ASSERT_TRUE(service.pump());
    EXPECT_EQ(service.m_nativeToken, replacement);
    ASSERT_TRUE(service.deliver(replacement, ClipboardStatus::Success, "survives cleanup"));
    ASSERT_EQ(service.poll(replacement, completion), ClipboardPollResult::Completed);
    EXPECT_EQ(completion.text, "survives cleanup");
}

TEST(ClipboardAsync, ReentrantStartCancellationDefersNewGenerationToNextPump){
    NWB::Tests::TestArena arena;
    DelayedClipboardService service(arena.arena);
    ClipboardCompletion completion(arena.arena);
    const auto oldAdmission = service.requestWriteText(ClipboardChannel::Clipboard, "old input");
    ASSERT_TRUE(oldAdmission);
    const ClipboardRequestToken old = *oldAdmission;
    ClipboardRequestToken replacement;
    service.m_onStart = [&](){
        if(service.m_nativeToken != old)
            return;
        ASSERT_TRUE(service.cancel(old));
        const auto replacementAdmission = service.requestWriteText(ClipboardChannel::Clipboard, "new input");
        ASSERT_TRUE(replacementAdmission);
        replacement = *replacementAdmission;
        EXPECT_FALSE(service.deliver(old, ClipboardStatus::Success));
        EXPECT_FALSE(service.pump());
    };

    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(service.pump()));
    EXPECT_EQ(service.m_startCount, 1u);
    EXPECT_EQ(service.poll(old, completion), ClipboardPollResult::InvalidRequest);
    EXPECT_EQ(service.poll(replacement, completion), ClipboardPollResult::Pending);
    service.m_onStart = {};
    ASSERT_TRUE(service.pump());
    EXPECT_EQ(service.m_startCount, 2u);
    EXPECT_EQ(service.m_nativeToken, replacement);
    EXPECT_EQ(service.m_startedText, "new input");
    ASSERT_TRUE(service.deliver(replacement, ClipboardStatus::Success));
}

TEST(ClipboardAsync, DelayedRepliesValidateUtf8AndOwnTheirCompletionBytes){
    NWB::Tests::TestArena arena;
    DelayedClipboardService service(arena.arena);
    ClipboardCompletion completion(arena.arena);
    const auto invalidAdmission = service.requestReadText(ClipboardChannel::Clipboard);
    ASSERT_TRUE(invalidAdmission);
    const ClipboardRequestToken invalid = *invalidAdmission;
    ASSERT_TRUE(service.pump());
    ASSERT_TRUE(service.deliver(invalid, ClipboardStatus::Success, "\xF0\x9F"));
    ASSERT_EQ(service.poll(invalid, completion), ClipboardPollResult::Completed);
    EXPECT_EQ(completion.status, ClipboardStatus::InvalidText);
    EXPECT_TRUE(completion.text.empty());

    const auto validAdmission = service.requestReadText(ClipboardChannel::Clipboard);
    ASSERT_TRUE(validAdmission);
    const ClipboardRequestToken valid = *validAdmission;
    ASSERT_TRUE(service.pump());
    AString<Alloc::GlobalArena> reply("\xED\x95\x9C\xF0\x9F\x98\x80", arena.arena);
    ASSERT_TRUE(service.deliver(valid, ClipboardStatus::Success, reply));
    reply.assign("changed source");
    ASSERT_EQ(service.poll(valid, completion), ClipboardPollResult::Completed);
    EXPECT_EQ(completion.status, ClipboardStatus::Success);
    EXPECT_EQ(completion.text, "\xED\x95\x9C\xF0\x9F\x98\x80");
    EXPECT_FALSE(service.deliver(valid, ClipboardStatus::Success, "duplicate reply"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/os/clipboard_text.h>

#include <tests/common/test_context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_clipboard_text_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core;

TEST(ClipboardText, Utf8RejectsMalformedUnicodeAndEmbeddedNull){
    EXPECT_EQ(ValidateClipboardUtf8Text({}), ClipboardStatus::Success);
    EXPECT_EQ(ValidateClipboardUtf8Text("\xF4\x8F\xBF\xBF"), ClipboardStatus::Success);
    const Array<AStringView, 7u> invalid = { "\x80", "\xC0\xAF", "\xE0\x80\xAF", "\xED\xA0\x80", "\xF4\x90\x80\x80", "\xF0\x9F", AStringView("before\0after", 12u), };
    for(const AStringView text : invalid)
        EXPECT_EQ(ValidateClipboardUtf8Text(text), ClipboardStatus::InvalidText);
}

TEST(ClipboardText, IncrementalChunksCanSplitUnicodeUntilFinalValidation){
    NWB::Tests::TestArena arena;
    ClipboardTextAccumulator accumulator(arena.arena);
    EXPECT_EQ(accumulator.appendBytes("\xED"), ClipboardStatus::Success);
    EXPECT_EQ(ValidateClipboardUtf8Text(accumulator.text()), ClipboardStatus::InvalidText);
    EXPECT_EQ(accumulator.appendBytes("\x95\x9C\xF0\x9F"), ClipboardStatus::Success);
    EXPECT_EQ(ValidateClipboardUtf8Text(accumulator.text()), ClipboardStatus::InvalidText);
    EXPECT_EQ(accumulator.appendBytes("\x98\x80"), ClipboardStatus::Success);
    EXPECT_EQ(ValidateClipboardUtf8Text(accumulator.text()), ClipboardStatus::Success);
    EXPECT_EQ(accumulator.text(), "\xED\x95\x9C\xF0\x9F\x98\x80");
}

TEST(ClipboardText, IncrementalFailuresDiscardBytesAndRemainStickyUntilClear){
    NWB::Tests::TestArena arena;
    ClipboardTextAccumulator accumulator(arena.arena);
    ASSERT_EQ(accumulator.appendBytes("prefix"), ClipboardStatus::Success);
    EXPECT_EQ(accumulator.appendBytes(AStringView("a\0b", 3u)), ClipboardStatus::InvalidText);
    EXPECT_TRUE(accumulator.text().empty());
    EXPECT_EQ(accumulator.appendBytes("ignored"), ClipboardStatus::InvalidText);
    EXPECT_TRUE(accumulator.text().empty());
    accumulator.clear();
    EXPECT_EQ(accumulator.appendBytes("fresh"), ClipboardStatus::Success);
    EXPECT_EQ(accumulator.text(), "fresh");
}

TEST(ClipboardText, ByteLimitIsInclusiveAndBoundsIncrementalAccumulation){
    NWB::Tests::TestArena arena;
    AString<Alloc::GlobalArena> maximum(s_ClipboardMaxTextBytes, 'x', arena.arena);
    EXPECT_EQ(ValidateClipboardUtf8Text(maximum), ClipboardStatus::Success);
    ClipboardTextAccumulator accumulator(arena.arena);
    ASSERT_EQ(accumulator.appendBytes(maximum), ClipboardStatus::Success);
    EXPECT_EQ(accumulator.text().size(), s_ClipboardMaxTextBytes);
    EXPECT_EQ(accumulator.appendBytes("x"), ClipboardStatus::TooLarge);
    EXPECT_TRUE(accumulator.text().empty());
    EXPECT_EQ(accumulator.appendBytes("ignored"), ClipboardStatus::TooLarge);
    maximum.push_back('x');
    EXPECT_EQ(ValidateClipboardUtf8Text(maximum), ClipboardStatus::TooLarge);
    accumulator.clear();
    EXPECT_EQ(accumulator.appendBytes("fresh"), ClipboardStatus::Success);
}

TEST(ClipboardText, Latin1UpperByteBoundsAndUnsupportedUnicodeClearOutput){
    NWB::Tests::TestArena arena;
    AString<Alloc::GlobalArena> utf8(arena.arena);
    AString<Alloc::GlobalArena> latin1(arena.arena);
    const AStringView source("\x80\xFF", 2u);
    ASSERT_EQ(DecodeClipboardLatin1(source, utf8), ClipboardStatus::Success);
    EXPECT_EQ(utf8, "\xC2\x80\xC3\xBF");
    ASSERT_EQ(EncodeClipboardLatin1(utf8, latin1), ClipboardStatus::Success);
    EXPECT_EQ(latin1, source);
    EXPECT_EQ(EncodeClipboardLatin1(AStringView("\xC4\x80\xFF", 3u), latin1), ClipboardStatus::InvalidText);
    EXPECT_TRUE(latin1.empty());
    EXPECT_EQ(EncodeClipboardLatin1("\xE2\x98\x83", latin1), ClipboardStatus::Unsupported);
    EXPECT_TRUE(latin1.empty());
    EXPECT_EQ(DecodeClipboardLatin1(AStringView("a\0b", 3u), utf8), ClipboardStatus::InvalidText);
    EXPECT_TRUE(utf8.empty());
}

TEST(ClipboardText, Latin1ConversionAcceptsAliasedOutputAndSubviews){
    NWB::Tests::TestArena arena;
    AString<Alloc::GlobalArena> text(AStringView("\x80\xFF", 2u), arena.arena);
    ASSERT_EQ(DecodeClipboardLatin1(AStringView(text), text), ClipboardStatus::Success);
    EXPECT_EQ(text, "\xC2\x80\xC3\xBF");
    ASSERT_EQ(EncodeClipboardLatin1(AStringView(text), text), ClipboardStatus::Success);
    EXPECT_EQ(AStringView(text), AStringView("\x80\xFF", 2u));

    text.assign("prefix\xC2\x80");
    const AStringView suffix = AStringView(text).substr(6u);
    ASSERT_EQ(EncodeClipboardLatin1(suffix, text), ClipboardStatus::Success);
    EXPECT_EQ(AStringView(text), AStringView("\x80", 1u));
}

TEST(ClipboardText, Latin1ExpansionCannotExceedUtf8ByteLimit){
    NWB::Tests::TestArena arena;
    AString<Alloc::GlobalArena> input(s_ClipboardMaxTextBytes / 2u + 1u, static_cast<char>(0xFFu), arena.arena);
    AString<Alloc::GlobalArena> output("stale bytes", arena.arena);
    EXPECT_EQ(DecodeClipboardLatin1(input, output), ClipboardStatus::TooLarge);
    EXPECT_TRUE(output.empty());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


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
    using QueuedTextInputService::cancelSession;
    using QueuedTextInputService::emitCommit;
    using QueuedTextInputService::emitPreedit;
    using QueuedTextInputService::emitDeleteSurrounding;
    using QueuedTextInputService::surroundingText;
    using QueuedTextInputService::surroundingCaretByte;
    using QueuedTextInputService::caretRect;


public:
    explicit FakeTextInputService(Alloc::GlobalArena& arena)
        : QueuedTextInputService(arena)
        , m_nativeSurrounding(arena)
    {}


public:
    [[nodiscard]] virtual TextInputCapabilities capabilities()const noexcept override{
        return m_supported ? TextInputCapabilities{ true, true, true, true } : TextInputCapabilities{};
    }


protected:
    [[nodiscard]] virtual TextInputAdmission::Enum startNativeSession(const TextInputSessionToken token, const TextInputSessionDesc& desc)override{
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

    virtual void updateNativeSurrounding(const AStringView text, usize, usize, u64, const TextInputChangeCause::Enum cause)override{
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
    usize m_endCount = 0u;
    TextInputAdmission::Enum m_startAdmission = TextInputAdmission::Accepted;
    TextInputChangeCause::Enum m_nativeCause = TextInputChangeCause::Other;
    b8 m_supported = true;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(TextInput, BeginRequiresFocusCapabilityAndOnlyOneActiveSession){
    NWB::Tests::TestArena arena;
    FakeTextInputService service(arena.arena);
    const auto unavailableBegin = service.begin({});
    ASSERT_FALSE(unavailableBegin);
    EXPECT_EQ(unavailableBegin.error(), TextInputAdmission::Unavailable);
    ASSERT_TRUE(service.setFocused(true));
    service.m_supported = false;
    const auto unsupportedBegin = service.begin({});
    ASSERT_FALSE(unsupportedBegin);
    EXPECT_EQ(unsupportedBegin.error(), TextInputAdmission::Unsupported);
    service.m_supported = true;
    const auto first = service.begin({});
    ASSERT_TRUE(first);
    ASSERT_TRUE(first->valid());
    EXPECT_EQ(service.activeSession(), *first);
    const auto busyBegin = service.begin({});
    ASSERT_FALSE(busyBegin);
    EXPECT_EQ(busyBegin.error(), TextInputAdmission::Busy);
    ASSERT_TRUE(service.end(*first));
    EXPECT_FALSE(service.activeSession().valid());
}

TEST(TextInput, InputsAndPolledEventsOwnCopiedUtf8Bytes){
    NWB::Tests::TestArena arena;
    FakeTextInputService service(arena.arena);
    ASSERT_TRUE(service.setFocused(true));
    AString<Alloc::GlobalArena> surrounding("A\xED\x95\x9C\xF0\x9F\x98\x80", arena.arena);
    const auto tokenAdmission = service.begin({ {}, surrounding, 1u, 4u });
    ASSERT_TRUE(tokenAdmission);
    const TextInputSessionToken token = *tokenAdmission;
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
    const auto oldAdmission = service.begin({});
    ASSERT_TRUE(oldAdmission);
    const TextInputSessionToken old = *oldAdmission;
    const auto otherAdmission = foreign.begin({});
    ASSERT_TRUE(otherAdmission);
    const TextInputSessionToken other = *otherAdmission;
    ASSERT_EQ(service.emitCommit(old, "queued"), TextInputAdmission::Accepted);
    ASSERT_TRUE(service.end(old));
    const auto currentAdmission = service.begin({});
    ASSERT_TRUE(currentAdmission);
    const TextInputSessionToken current = *currentAdmission;
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
    const auto tokenAdmission = service.begin({});
    ASSERT_TRUE(tokenAdmission);
    const TextInputSessionToken token = *tokenAdmission;
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
    const auto unavailableBegin = service.begin({});
    ASSERT_FALSE(unavailableBegin);
    EXPECT_EQ(unavailableBegin.error(), TextInputAdmission::Unavailable);
    ASSERT_TRUE(service.setFocused(true));
    const auto resumed = service.begin({});
    ASSERT_TRUE(resumed);
    EXPECT_TRUE(resumed->valid());
}

TEST(TextInput, NewSessionInvalidatesUnpolledCancellation){
    NWB::Tests::TestArena arena;
    FakeTextInputService service(arena.arena);
    ASSERT_TRUE(service.setFocused(true));
    const auto oldAdmission = service.begin({});
    ASSERT_TRUE(oldAdmission);
    const TextInputSessionToken old = *oldAdmission;
    ASSERT_TRUE(service.cancelSession(old, TextInputCancelReason::NativeCancelled));
    const auto currentAdmission = service.begin({});
    ASSERT_TRUE(currentAdmission);
    const TextInputSessionToken current = *currentAdmission;
    TextInputEvent event(arena.arena);
    EXPECT_EQ(service.poll(old, event), TextInputPollResult::InvalidSession);
    EXPECT_EQ(service.poll(current, event), TextInputPollResult::Pending);
}

TEST(TextInput, ConsecutivePreeditCoalescesWithoutCrossingCommitOrDeletion){
    NWB::Tests::TestArena arena;
    FakeTextInputService service(arena.arena);
    ASSERT_TRUE(service.setFocused(true));
    const auto tokenAdmission = service.begin({ {}, "abc", 1u, 1u });
    ASSERT_TRUE(tokenAdmission);
    const TextInputSessionToken token = *tokenAdmission;
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
    const auto tokenAdmission = service.begin({});
    ASSERT_TRUE(tokenAdmission);
    const TextInputSessionToken token = *tokenAdmission;
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
    const auto tokenAdmission = service.begin({});
    ASSERT_TRUE(tokenAdmission);
    const TextInputSessionToken token = *tokenAdmission;
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
    const auto tokenAdmission = service.begin({ {}, "A\xED\x95\x9C" "B", 4u, 4u });
    ASSERT_TRUE(tokenAdmission);
    const TextInputSessionToken token = *tokenAdmission;
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
    const auto tokenAdmission = service.begin({});
    ASSERT_TRUE(tokenAdmission);
    const TextInputSessionToken token = *tokenAdmission;
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
    const auto tokenAdmission = service.begin({});
    ASSERT_TRUE(tokenAdmission);
    const TextInputSessionToken token = *tokenAdmission;
    service.m_onEnd = [&](const TextInputSessionToken ended){
        EXPECT_EQ(ended, token);
        EXPECT_EQ(service.emitCommit(ended, "reentrant late"), TextInputAdmission::InvalidSession);
        const auto busyBegin = service.begin({});
        ASSERT_FALSE(busyBegin);
        EXPECT_EQ(busyBegin.error(), TextInputAdmission::Busy);
        EXPECT_FALSE(service.end(ended));
    };
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(service.cancelSession(token, TextInputCancelReason::NativeCancelled)));
    EXPECT_EQ(service.m_endCount, 1u);
    service.m_onEnd = {};
    const auto resumed = service.begin({});
    ASSERT_TRUE(resumed);
    ASSERT_TRUE(resumed->valid());
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
    const auto nativeFailureBegin = service.begin({});
    ASSERT_FALSE(nativeFailureBegin);
    EXPECT_EQ(nativeFailureBegin.error(), TextInputAdmission::NativeFailure);
    EXPECT_FALSE(service.activeSession().valid());
    TextInputEvent event(arena.arena);
    EXPECT_EQ(service.poll(failed, event), TextInputPollResult::InvalidSession);
    EXPECT_EQ(service.m_endCount, 1u);
}

TEST(TextInput, WrongThreadCannotMutateOrPollOwnerSession){
    NWB::Tests::TestArena arena;
    FakeTextInputService service(arena.arena);
    ASSERT_TRUE(service.setFocused(true));
    const auto tokenAdmission = service.begin({});
    ASSERT_TRUE(tokenAdmission);
    const TextInputSessionToken token = *tokenAdmission;
    Thread worker([&](){
        TextInputEvent event(arena.arena);
        EXPECT_FALSE(service.isOwnerThread());
        EXPECT_FALSE(service.activeSession().valid());
        EXPECT_EQ(service.surroundingRevision(token), 0u);
        const auto wrongThreadBegin = service.begin({});
        ASSERT_FALSE(wrongThreadBegin);
        EXPECT_EQ(wrongThreadBegin.error(), TextInputAdmission::WrongThread);
        EXPECT_EQ(service.emitCommit(token, "wrong thread"), TextInputAdmission::WrongThread);
        EXPECT_EQ(service.poll(token, event), TextInputPollResult::WrongThread);
        EXPECT_EQ(service.updateCaret(token, {}), TextInputAdmission::WrongThread);
        EXPECT_EQ(service.updateSurrounding(token, {}, 0u, 0u), TextInputAdmission::WrongThread);
        EXPECT_FALSE(service.setFocused(false));
        EXPECT_FALSE(service.end(token));
    });
    worker.join();
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    EXPECT_EQ(service.activeSession(), token);
    TextInputEvent event(arena.arena);
    EXPECT_EQ(service.poll(token, event), TextInputPollResult::Pending);
}

TEST(TextInput, NullNativeFactoryReportsUnsupportedWithoutClaimingTextInput){
    NWB::Tests::TestArena arena;
    auto serviceResult = CreateTextInputService(arena.arena, nullptr);
    ASSERT_TRUE(serviceResult);
    const GlobalUniquePtr<ITextInputService> service = Move(*serviceResult);
    ASSERT_NE(service.get(), nullptr);
    EXPECT_FALSE(service->capabilities().commit);
    EXPECT_FALSE(service->capabilities().preedit);
    EXPECT_FALSE(service->capabilities().surrounding);
    EXPECT_FALSE(service->capabilities().deleteSurrounding);
    ASSERT_TRUE(service->setFocused(true));
    const auto unsupportedBegin = service->begin({});
    ASSERT_FALSE(unsupportedBegin);
    EXPECT_EQ(unsupportedBegin.error(), TextInputAdmission::Unsupported);
}

TEST(TextInput, ReentrantNativeCaretCancellationReturnsFailureAndRetainsTerminalEvent){
    NWB::Tests::TestArena arena;
    FakeTextInputService service(arena.arena);
    ASSERT_TRUE(service.setFocused(true));
    const auto tokenAdmission = service.begin({});
    ASSERT_TRUE(tokenAdmission);
    const TextInputSessionToken token = *tokenAdmission;
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
    const auto tokenAdmission = service.begin({});
    ASSERT_TRUE(tokenAdmission);
    const TextInputSessionToken token = *tokenAdmission;
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
    const auto tokenAdmission = service.begin({});
    ASSERT_TRUE(tokenAdmission);
    const TextInputSessionToken token = *tokenAdmission;
    ASSERT_EQ(service.updateSurrounding(token, "local change", 0u, 12u), TextInputAdmission::Accepted);
    const u64 revision = service.surroundingRevision(token);
    EXPECT_EQ(service.updateSurrounding(token, "invalid", 0u, 0u, static_cast<TextInputChangeCause::Enum>(255u)), TextInputAdmission::InvalidRange);
    EXPECT_EQ(service.surroundingText(), "local change");
    EXPECT_EQ(service.surroundingRevision(token), revision);
    EXPECT_EQ(service.m_nativeCause, TextInputChangeCause::Other);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/os/text_input_text.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_text_input_text_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(TextInputText, ValidatesStrictUtf8AndEmbeddedNul){
    EXPECT_EQ(ValidateTextInputUtf8({}, 100u), TextInputAdmission::Accepted);
    EXPECT_EQ(ValidateTextInputUtf8("\xC0\x80", 100u), TextInputAdmission::InvalidText);
    EXPECT_EQ(ValidateTextInputUtf8("\xE0\x80\x80", 100u), TextInputAdmission::InvalidText);
    EXPECT_EQ(ValidateTextInputUtf8("\xED\xA0\x80", 100u), TextInputAdmission::InvalidText);
    EXPECT_EQ(ValidateTextInputUtf8("\xF4\x90\x80\x80", 100u), TextInputAdmission::InvalidText);
    EXPECT_EQ(ValidateTextInputUtf8("\xF0\x9F", 100u), TextInputAdmission::InvalidText);
    EXPECT_EQ(ValidateTextInputUtf8("\x80", 100u), TextInputAdmission::InvalidText);
    EXPECT_EQ(ValidateTextInputUtf8(AStringView("a\0b", 3u), 100u), TextInputAdmission::InvalidText);
    EXPECT_EQ(ValidateTextInputUtf8("four", 3u), TextInputAdmission::TooLarge);
}

TEST(TextInputText, BoundariesUseBytesForBmpAndSupplementaryScalars){
    constexpr AStringView s_Text = "A\xED\x95\x9C\xF0\x9F\x98\x80";
    for(const usize index : { 0u, 1u, 4u, 8u })
        EXPECT_TRUE(IsUtf8ScalarBoundary(s_Text, index));
    for(const usize index : { 2u, 3u, 5u, 6u, 7u, 9u })
        EXPECT_FALSE(IsUtf8ScalarBoundary(s_Text, index));
}

TEST(TextInputText, CaretBoundsRejectEmptyAndOverflowWhileAllowingClippedOrigins){
    EXPECT_TRUE(IsTextInputCaretRectValid({ -20, -10, 2, 20 }));
    EXPECT_TRUE(IsTextInputCaretRectValid({ Limit<i32>::s_Max - 1, Limit<i32>::s_Max - 1, 1, 1 }));
    EXPECT_FALSE(IsTextInputCaretRectValid({ 0, 0, 0, 1 }));
    EXPECT_FALSE(IsTextInputCaretRectValid({ 0, 0, 1, -1 }));
    EXPECT_FALSE(IsTextInputCaretRectValid({ Limit<i32>::s_Max, 0, 1, 1 }));
    EXPECT_FALSE(IsTextInputCaretRectValid({ 0, Limit<i32>::s_Max - 10, 1, 11 }));
}

TEST(TextInputText, RejectsInvalidScalarsWithoutPublishingAnEncodedLength){
    Array<char, s_Utf8MaxSequenceBytes> bytes = {};
    const auto length = EncodeTextInputCodePoint(0x1f600u, bytes);
    ASSERT_TRUE(length);
    EXPECT_EQ(AStringView(bytes.data(), *length), "\xF0\x9F\x98\x80");
    const auto invalid0 = EncodeTextInputCodePoint(0u, bytes);
    ASSERT_FALSE(invalid0);
    EXPECT_EQ(invalid0.error(), TextInputAdmission::InvalidText);
    const auto invalidd800 = EncodeTextInputCodePoint(0xd800u, bytes);
    ASSERT_FALSE(invalidd800);
    EXPECT_EQ(invalidd800.error(), TextInputAdmission::InvalidText);
    const auto invalid110000 = EncodeTextInputCodePoint(0x110000u, bytes);
    ASSERT_FALSE(invalid110000);
    EXPECT_EQ(invalid110000.error(), TextInputAdmission::InvalidText);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


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


#include <core/os/linux/wayland/text_input_state.h>
#include <core/os/text_input_text.h>

#include <tests/common/test_context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_wayland_text_input_state_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core;

TEST(WaylandTextInputState, ShortUtf8ReverseSelectionRetainsScalarByteEndpoints){
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
    EXPECT_TRUE(IsUtf8ScalarBoundary(slice.text, slice.anchorByte));
    EXPECT_TRUE(IsUtf8ScalarBoundary(slice.text, slice.caretByte));
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

TEST(WaylandTextInputState, PositiveOddPixelEdgesRoundOutwardAndZeroScaleUsesPixelBounds){
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


#if defined(NWB_PLATFORM_WINDOWS)

#include <core/os/win32/text_input.h>
#include <core/os/win32/text_input_context.h>

#include <tests/common/capturing_logger.h>
#include <tests/common/test_context.h>

#include <gtest/gtest.h>

#include <windows.h>
#include <imm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_win32_text_input_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core;
inline constexpr WStringView s_ClassName = L"NWBTextInputUnitTest";

class Win32TextInputFixture : public testing::Test{
public:
    static LRESULT CALLBACK WindowProc(const HWND window, const UINT message, const WPARAM wParam, const LPARAM lParam){
        if(message == WM_NCCREATE){
            const auto& creation = *MakeNotNull(reinterpret_cast<const CREATESTRUCTW*>(lParam));
            SetLastError(ERROR_SUCCESS);
            const LONG_PTR previous = SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(creation.lpCreateParams));
            if(previous == 0 && GetLastError() != ERROR_SUCCESS)
                return FALSE;
        }
        auto* const fixture = reinterpret_cast<Win32TextInputFixture*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if(fixture && fixture->m_service){
            const auto forwardedLParam = ResolveWin32TextInputContextMessage(*fixture->m_service, message, wParam, lParam);
            if(forwardedLParam){
                ++fixture->m_contextForwardCount;
                fixture->m_lastContextForwardedFlags = *forwardedLParam;
                if(fixture->m_cancelOnNextContextForward){
                    fixture->m_cancelOnNextContextForward = false;
                    fixture->m_cancelledDuringContextToken = fixture->m_service->activeSession();
                    fixture->m_contextFocusLossSucceeded = fixture->m_service->setFocused(false);
                    fixture->m_contextFocusRestoreSucceeded = fixture->m_service->setFocused(true);
                }
                fixture->m_lastContextDefaultResult = DefWindowProcW(window, message, wParam, static_cast<LPARAM>(*forwardedLParam));
                return fixture->m_lastContextDefaultResult;
            }
            if(message == WM_UNICHAR && wParam == UNICODE_NOCHAR)
                return TRUE;
            if(DispatchWin32TextInputMessage(*fixture->m_service, message, wParam, lParam))
                return 0;
            if(message == WM_CHAR){
                const auto codePoint = DecodeWin32FallbackCharInput(*fixture->m_service, static_cast<u32>(wParam));
                if(codePoint){
                    ++fixture->m_sceneCharacterCount;
                    fixture->m_sceneCodePoint = *codePoint;
                }
                return 0;
            }
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }


public:
    Win32TextInputFixture()
        : m_loggerGuard(m_logger, Common::LoggerBreakPolicy::ReportOnly)
    {}


protected:
    virtual void SetUp()override{
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSW nativeClass = {};
        nativeClass.lpfnWndProc = WindowProc;
        nativeClass.hInstance = instance;
        nativeClass.lpszClassName = s_ClassName.data();
        const ATOM registered = RegisterClassW(&nativeClass);
        ASSERT_TRUE(registered != 0u || GetLastError() == ERROR_CLASS_ALREADY_EXISTS);
        m_window = CreateWindowExW(0u, s_ClassName.data(), L"", WS_POPUP, 0, 0, 100, 100, nullptr, nullptr, instance, this);
        ASSERT_NE(m_window, nullptr);
        auto serviceResult = CreateTextInputService(m_arena.arena, m_window);
        ASSERT_TRUE(serviceResult);
        m_service = Move(*serviceResult);
        ASSERT_NE(m_service.get(), nullptr);
        ASSERT_TRUE(m_service->setFocused(true));
        const TextInputSessionDesc desc{ { 10, 15, 2, 18 }, {}, 0u, 0u };
        const auto begun = m_service->begin(desc);
        ASSERT_TRUE(begun);
        m_token = *begun;
        ASSERT_TRUE(m_token.valid());
    }

    virtual void TearDown()override{
        m_service.reset();
        if(m_window){
            EXPECT_NE(DestroyWindow(m_window), FALSE);
            m_window = nullptr;
        }
        EXPECT_EQ(m_logger.errorCount(), 0u);
        EXPECT_NE(m_logger.lastType(), Common::LogType::Fatal);
    }


protected:
    NWB::Tests::CapturingLogger m_logger;
    Common::LoggerRegistrationGuard m_loggerGuard;
    NWB::Tests::TestArena<> m_arena;
    GlobalUniquePtr<ITextInputService> m_service;
    HWND m_window = nullptr;
    TextInputSessionToken m_token;
    usize m_sceneCharacterCount = 0u;
    usize m_contextForwardCount = 0u;
    isize m_lastContextForwardedFlags = 0;
    LRESULT m_lastContextDefaultResult = 0;
    TextInputSessionToken m_cancelledDuringContextToken;
    u32 m_sceneCodePoint = 0u;
    b8 m_cancelOnNextContextForward = false;
    b8 m_contextFocusLossSucceeded = false;
    b8 m_contextFocusRestoreSucceeded = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(Win32TextInputFixture, RepeatedExplicitContextReleasePreservesActiveNativeSession){
    const HIMC nativeContext = ImmGetContext(m_window);
    ASSERT_NE(nativeContext, nullptr);
    Win32TextInputContextGuard context(m_window, nativeContext);
    ASSERT_TRUE(context.release());
    ASSERT_TRUE(context.release());
    EXPECT_EQ(m_service->activeSession(), m_token);
    EXPECT_EQ(SendMessageW(m_window, WM_CHAR, 'R', 0), 0);
    TextInputEvent event(m_arena.arena);
    ASSERT_EQ(m_service->poll(m_token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.text, "R");
}

TEST_F(Win32TextInputFixture, ContextScopeUnwindPreservesOriginalThrownMarkerAndNativeSession){
    struct UnwindMarker final{};
    b8 caughtOriginal = false;
    try{
        const HIMC nativeContext = ImmGetContext(m_window);
        ASSERT_NE(nativeContext, nullptr);
        Win32TextInputContextGuard context(m_window, nativeContext);
        throw UnwindMarker{};
    }
    catch(const UnwindMarker&){
        caughtOriginal = true;
    }
    EXPECT_TRUE(caughtOriginal);
    EXPECT_EQ(m_service->activeSession(), m_token);
    EXPECT_EQ(SendMessageW(m_window, WM_CHAR, 'U', 0), 0);
    TextInputEvent event(m_arena.arena);
    ASSERT_EQ(m_service->poll(m_token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.text, "U");
}


TEST_F(Win32TextInputFixture, SurrogatePairRemainsPerServiceUntilComplete){
    EXPECT_EQ(SendMessageW(m_window, WM_CHAR, 0xd83du, 0), 0);
    TextInputEvent event(m_arena.arena);
    EXPECT_EQ(m_service->poll(m_token, event), TextInputPollResult::Pending);
    EXPECT_EQ(SendMessageW(m_window, WM_CHAR, 0xde00u, 0), 0);
    ASSERT_EQ(m_service->poll(m_token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.text, "\xF0\x9F\x98\x80");
    EXPECT_EQ(m_service->poll(m_token, event), TextInputPollResult::Pending);
}

TEST_F(Win32TextInputFixture, AggregateCharacterCountsIgnoreHighFlagsAndKeepScalarBoundaries){
    EXPECT_EQ(SendMessageW(m_window, WM_CHAR, 'A', 0x40000003), 0);
    EXPECT_EQ(SendMessageW(m_window, WM_SYSCHAR, 0xd55cu, 0x20000002), 0);
    EXPECT_EQ(SendMessageW(m_window, WM_UNICHAR, 0x1f600u, 2), 0);
    EXPECT_EQ(SendMessageW(m_window, WM_CHAR, 0xd83du, 2), 0);
    EXPECT_EQ(SendMessageW(m_window, WM_CHAR, 0xde00u, 2), 0);
    TextInputEvent event(m_arena.arena);
    ASSERT_EQ(m_service->poll(m_token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.text, "AAA");
    ASSERT_EQ(m_service->poll(m_token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.text, "\xED\x95\x9C\xED\x95\x9C");
    for(usize index = 0u; index < 2u; ++index){
        ASSERT_EQ(m_service->poll(m_token, event), TextInputPollResult::Event);
        EXPECT_EQ(event.text, "\xF0\x9F\x98\x80\xF0\x9F\x98\x80");
    }
    EXPECT_EQ(m_service->poll(m_token, event), TextInputPollResult::Pending);
}

TEST_F(Win32TextInputFixture, MaximumAggregateSupplementaryCommitBatchesWithoutEventOverflow){
    EXPECT_EQ(SendMessageW(m_window, WM_UNICHAR, 0x1f600u, 0xffff), 0);
    ASSERT_EQ(m_service->activeSession(), m_token);
    TextInputEvent event(m_arena.arena);
    usize totalBytes = 0u;
    u64 sequence = 0u;
    while(totalBytes < 0xffffu * 4u){
        ASSERT_EQ(m_service->poll(m_token, event), TextInputPollResult::Event);
        ASSERT_EQ(event.kind, TextInputEventKind::Commit);
        ASSERT_FALSE(event.text.empty());
        EXPECT_LE(event.text.size(), s_TextInputMaxEventTextBytes);
        ASSERT_EQ(event.text.size() % 4u, 0u);
        EXPECT_GT(event.sequence, sequence);
        sequence = event.sequence;
        for(usize offset = 0u; offset < event.text.size(); offset += 4u)
            ASSERT_EQ(AStringView(event.text.data() + offset, 4u), "\xF0\x9F\x98\x80");
        totalBytes += event.text.size();
    }
    EXPECT_EQ(totalBytes, 0xffffu * 4u);
    EXPECT_EQ(m_service->poll(m_token, event), TextInputPollResult::Pending);
}

TEST_F(Win32TextInputFixture, AggregateCommitOverflowDiscardsAlreadyQueuedPrefix){
    EXPECT_EQ(SendMessageW(m_window, WM_CHAR, 'x', 5), 0);
    EXPECT_EQ(SendMessageW(m_window, WM_UNICHAR, 0x1f600u, 0xffff), 0);
    EXPECT_FALSE(m_service->activeSession().valid());
    TextInputEvent event(m_arena.arena);
    ASSERT_EQ(m_service->poll(m_token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.kind, TextInputEventKind::Cancelled);
    EXPECT_EQ(event.cancelReason, TextInputCancelReason::Overflow);
    EXPECT_TRUE(event.text.empty());
    EXPECT_EQ(m_service->poll(m_token, event), TextInputPollResult::InvalidSession);
}

TEST_F(Win32TextInputFixture, IsolatedLowSurrogateCancelsWithExplicitFailure){
    EXPECT_EQ(SendMessageW(m_window, WM_CHAR, 0xde00u, 0), 0);
    EXPECT_FALSE(m_service->activeSession().valid());
    TextInputEvent event(m_arena.arena);
    ASSERT_EQ(m_service->poll(m_token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.kind, TextInputEventKind::Cancelled);
    EXPECT_EQ(event.cancelReason, TextInputCancelReason::NativeFailure);
    EXPECT_TRUE(event.text.empty());
#if NWB_OCCUR_WARNING
    EXPECT_TRUE(m_logger.sawMessageContaining(NWB_TEXT("Text input: native delivery rejected")));
    EXPECT_EQ(m_logger.lastType(), Common::LogType::Warning);
#endif
}

TEST_F(Win32TextInputFixture, FocusLossClearsPendingSurrogateBeforeReplacement){
    EXPECT_EQ(SendMessageW(m_window, WM_CHAR, 0xd83du, 0), 0);
    ASSERT_TRUE(m_service->setFocused(false));
    TextInputEvent event(m_arena.arena);
    ASSERT_EQ(m_service->poll(m_token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.cancelReason, TextInputCancelReason::FocusLost);
    EXPECT_FALSE(DispatchWin32TextInputMessage(*m_service, WM_CHAR, 'x', 0));
    ASSERT_TRUE(m_service->setFocused(true));
    const auto tokenAdmission = m_service->begin({});
    ASSERT_TRUE(tokenAdmission);
    m_token = *tokenAdmission;
    ASSERT_TRUE(m_token.valid());
    EXPECT_EQ(SendMessageW(m_window, WM_CHAR, 'B', 0), 0);
    ASSERT_EQ(m_service->poll(m_token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.text, "B");
}

TEST_F(Win32TextInputFixture, EndDiscardAndReplacementCannotReusePartialUtf16){
    EXPECT_EQ(SendMessageW(m_window, WM_CHAR, 0xd83du, 0), 0);
    ASSERT_TRUE(m_service->end(m_token));
    const auto tokenAdmission = m_service->begin({});
    ASSERT_TRUE(tokenAdmission);
    m_token = *tokenAdmission;
    ASSERT_TRUE(m_token.valid());
    EXPECT_EQ(SendMessageW(m_window, WM_CHAR, 0xde00u, 0), 0);
    TextInputEvent event(m_arena.arena);
    ASSERT_EQ(m_service->poll(m_token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.kind, TextInputEventKind::Cancelled);
}

TEST_F(Win32TextInputFixture, ControlCharactersStayOutOfCommittedDocumentText){
    for(const WPARAM character : { 0x08u, 0x09u, 0x0du, 0x1bu, 0x7fu })
        EXPECT_EQ(SendMessageW(m_window, WM_CHAR, character, 0), 0);
    TextInputEvent event(m_arena.arena);
    EXPECT_EQ(m_service->poll(m_token, event), TextInputPollResult::Pending);
    EXPECT_EQ(SendMessageW(m_window, WM_UNICHAR, UNICODE_NOCHAR, 0), TRUE);
    EXPECT_EQ(m_service->poll(m_token, event), TextInputPollResult::Pending);
}

TEST_F(Win32TextInputFixture, ImeCharIsConsumedWithoutDuplicateCommitAndFollowingTypingSurvives){
    EXPECT_EQ(SendMessageW(m_window, WM_IME_STARTCOMPOSITION, 0u, 0), 0);
    EXPECT_EQ(SendMessageW(m_window, WM_IME_CHAR, 0xd55cu, 0), 0);
    EXPECT_EQ(SendMessageW(m_window, WM_IME_ENDCOMPOSITION, 0u, 0), 0);
    TextInputEvent event(m_arena.arena);
    ASSERT_EQ(m_service->poll(m_token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.kind, TextInputEventKind::Preedit);
    EXPECT_TRUE(event.text.empty());
    EXPECT_EQ(m_service->poll(m_token, event), TextInputPollResult::Pending);
    EXPECT_EQ(SendMessageW(m_window, WM_CHAR, 0xd55cu, 0), 0);
    ASSERT_EQ(m_service->poll(m_token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.kind, TextInputEventKind::Commit);
    EXPECT_EQ(event.text, "\xED\x95\x9C");
}

TEST_F(Win32TextInputFixture, CompositionWithoutCurrentStartCannotDeliverAcrossSessionBoundary){
    EXPECT_EQ(SendMessageW(m_window, WM_IME_STARTCOMPOSITION, 0u, 0), 0);
    ASSERT_TRUE(m_service->end(m_token));
    const auto tokenAdmission = m_service->begin({});
    ASSERT_TRUE(tokenAdmission);
    m_token = *tokenAdmission;
    ASSERT_TRUE(m_token.valid());
    EXPECT_EQ(SendMessageW(m_window, WM_IME_COMPOSITION, 0u, GCS_RESULTSTR), 0);
    TextInputEvent event(m_arena.arena);
    EXPECT_EQ(m_service->poll(m_token, event), TextInputPollResult::Pending);
}

TEST_F(Win32TextInputFixture, BridgeRejectsUnsupportedForeignServiceBeforeDowncast){
    auto unsupportedResult = CreateTextInputService(m_arena.arena, nullptr);
    ASSERT_TRUE(unsupportedResult);
    const GlobalUniquePtr<ITextInputService> unsupported = Move(*unsupportedResult);
    EXPECT_FALSE(DispatchWin32TextInputMessage(*unsupported, WM_CHAR, 'x', 0));
    EXPECT_FALSE(DispatchWin32TextInputMessage(*m_service, WM_MOUSEMOVE, 0u, 0));
    EXPECT_FALSE(ResolveWin32TextInputContextMessage(*unsupported, WM_IME_SETCONTEXT, TRUE, 456));
    EXPECT_FALSE(ResolveWin32TextInputContextMessage(*m_service, WM_MOUSEMOVE, 0u, 456));
    TextInputEvent event(m_arena.arena);
    EXPECT_EQ(m_service->poll(m_token, event), TextInputPollResult::Pending);
}

TEST_F(Win32TextInputFixture, ImeContextVisibilityTracksSessionAfterActivationAndPreservesOtherFlags){
    ASSERT_TRUE(m_service->end(m_token));
    const isize flags = static_cast<isize>(ISC_SHOWUICOMPOSITIONWINDOW | ISC_SHOWUICANDIDATEWINDOW | (ISC_SHOWUICANDIDATEWINDOW << 2u) | (1u << 20u));
    const usize before = m_contextForwardCount;
    const LRESULT inactiveResult = SendMessageW(m_window, WM_IME_SETCONTEXT, TRUE, static_cast<LPARAM>(flags));
    EXPECT_EQ(m_contextForwardCount, before + 1u);
    EXPECT_EQ(m_lastContextForwardedFlags, flags);
    EXPECT_EQ(inactiveResult, m_lastContextDefaultResult);

    const auto begun = m_service->begin({});
    ASSERT_TRUE(begun);
    m_token = *begun;
    EXPECT_EQ(m_contextForwardCount, before + 2u);
    EXPECT_EQ(m_lastContextForwardedFlags, flags & ~static_cast<isize>(ISC_SHOWUICOMPOSITIONWINDOW));
    const LRESULT activeResult = SendMessageW(m_window, WM_IME_SETCONTEXT, TRUE, static_cast<LPARAM>(flags));
    EXPECT_EQ(m_contextForwardCount, before + 3u);
    EXPECT_EQ(m_lastContextForwardedFlags, flags & ~static_cast<isize>(ISC_SHOWUICOMPOSITIONWINDOW));
    EXPECT_EQ(activeResult, m_lastContextDefaultResult);
    ASSERT_TRUE(m_service->end(m_token));
    EXPECT_EQ(m_contextForwardCount, before + 4u);
    EXPECT_EQ(m_lastContextForwardedFlags, flags);

}

TEST_F(Win32TextInputFixture, ImeContextReplayNeverReactivatesAfterFocusLossOrNativeDeactivation){
    ASSERT_TRUE(m_service->end(m_token));
    const isize flags = static_cast<isize>(ISC_SHOWUICOMPOSITIONWINDOW | ISC_SHOWUICANDIDATEWINDOW | (1u << 20u));
    SendMessageW(m_window, WM_IME_SETCONTEXT, TRUE, static_cast<LPARAM>(flags));
    const auto begun = m_service->begin({});
    ASSERT_TRUE(begun);
    m_token = *begun;
    const usize beforeFocusLoss = m_contextForwardCount;
    ASSERT_TRUE(m_service->setFocused(false));
    EXPECT_EQ(m_contextForwardCount, beforeFocusLoss);
    const LRESULT deactivationResult = SendMessageW(m_window, WM_IME_SETCONTEXT, FALSE, static_cast<LPARAM>(flags));
    EXPECT_EQ(m_contextForwardCount, beforeFocusLoss + 1u);
    EXPECT_EQ(m_lastContextForwardedFlags, flags);
    EXPECT_EQ(deactivationResult, m_lastContextDefaultResult);

    ASSERT_TRUE(m_service->setFocused(true));
    const auto second = m_service->begin({});
    ASSERT_TRUE(second);
    m_token = *second;
    EXPECT_EQ(m_contextForwardCount, beforeFocusLoss + 1u);
    ASSERT_TRUE(m_service->end(m_token));
    EXPECT_EQ(m_contextForwardCount, beforeFocusLoss + 1u);
}

TEST_F(Win32TextInputFixture, ReentrantFocusCancellationRestoresNativeImeVisibilityAndKeepsCancelEvent){
    ASSERT_TRUE(m_service->end(m_token));
    const isize flags = static_cast<isize>(ISC_SHOWUICOMPOSITIONWINDOW | ISC_SHOWUICANDIDATEWINDOW | (1u << 20u));
    SendMessageW(m_window, WM_IME_SETCONTEXT, TRUE, static_cast<LPARAM>(flags));
    const usize before = m_contextForwardCount;
    m_cancelOnNextContextForward = true;
    const auto begun = m_service->begin({});
    ASSERT_FALSE(begun);
    EXPECT_EQ(begun.error(), TextInputAdmission::Unavailable);
    EXPECT_FALSE(begun.has_value());
    EXPECT_FALSE(m_cancelOnNextContextForward);
    EXPECT_TRUE(m_contextFocusLossSucceeded);
    EXPECT_TRUE(m_contextFocusRestoreSucceeded);
    EXPECT_TRUE(m_cancelledDuringContextToken.valid());
    EXPECT_EQ(m_contextForwardCount, before + 2u);
    EXPECT_EQ(m_lastContextForwardedFlags, flags);

    TextInputEvent event(m_arena.arena);
    ASSERT_EQ(m_service->poll(m_cancelledDuringContextToken, event), TextInputPollResult::Event);
    EXPECT_EQ(event.kind, TextInputEventKind::Cancelled);
    EXPECT_EQ(event.cancelReason, TextInputCancelReason::FocusLost);
    EXPECT_EQ(m_service->poll(m_cancelledDuringContextToken, event), TextInputPollResult::InvalidSession);
    EXPECT_EQ(m_sceneCharacterCount, 0u);
}

TEST_F(Win32TextInputFixture, SceneFallbackCannotJoinSurrogatesAcrossSessionOwnership){
    ASSERT_TRUE(m_service->end(m_token));
    Expected<u32> codePoint = MakeUnexpected(Failure{});
    EXPECT_FALSE((codePoint = DecodeWin32FallbackCharInput(*m_service, 0xd83du)));

    const auto tokenAdmission = m_service->begin({});
    ASSERT_TRUE(tokenAdmission);
    m_token = *tokenAdmission;
    ASSERT_TRUE(m_token.valid());
    ASSERT_TRUE(m_service->end(m_token));
    EXPECT_FALSE((codePoint = DecodeWin32FallbackCharInput(*m_service, 0xde00u)));

    ASSERT_TRUE((codePoint = DecodeWin32FallbackCharInput(*m_service, 'A')));
    EXPECT_EQ(*codePoint, static_cast<u32>('A'));
}

TEST_F(Win32TextInputFixture, SceneFallbackFocusResetDiscardsPartialPair){
    ASSERT_TRUE(m_service->end(m_token));
    EXPECT_FALSE(DecodeWin32FallbackCharInput(*m_service, 0xd83du));
    ASSERT_TRUE(m_service->setFocused(false));
    ASSERT_TRUE(ResetWin32FallbackCharInput(*m_service));
    ASSERT_TRUE(m_service->setFocused(true));
    ASSERT_TRUE(ResetWin32FallbackCharInput(*m_service));
    EXPECT_FALSE(DecodeWin32FallbackCharInput(*m_service, 0xde00u));
}

TEST_F(Win32TextInputFixture, SceneFallbackDecoderNeverConsumesActivePair){
    EXPECT_EQ(SendMessageW(m_window, WM_CHAR, 0xd83du, 0), 0);
    EXPECT_FALSE(DecodeWin32FallbackCharInput(*m_service, 0xde00u));

    EXPECT_EQ(SendMessageW(m_window, WM_CHAR, 0xde00u, 0), 0);
    TextInputEvent event(m_arena.arena);
    ASSERT_EQ(m_service->poll(m_token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.text, "\xF0\x9F\x98\x80");
}

TEST_F(Win32TextInputFixture, SceneFallbackSurrogateStateIsPerServiceAndValidatesUnits){
    ASSERT_TRUE(m_service->end(m_token));
    auto secondResult = CreateTextInputService(m_arena.arena, m_window);
    ASSERT_TRUE(secondResult);
    const GlobalUniquePtr<ITextInputService> second = Move(*secondResult);
    ASSERT_NE(second.get(), nullptr);
    ASSERT_TRUE(second->setFocused(true));
    EXPECT_FALSE(DecodeWin32FallbackCharInput(*m_service, 0xd83du));
    EXPECT_FALSE(DecodeWin32FallbackCharInput(*second, 0xde00u));
    Expected<u32> codePoint = DecodeWin32FallbackCharInput(*m_service, 0xde00u);
    ASSERT_TRUE(codePoint);
    EXPECT_EQ(*codePoint, 0x1f600u);
    EXPECT_FALSE(DecodeWin32FallbackCharInput(*m_service, 0x110000u));

    EXPECT_FALSE(DecodeWin32FallbackCharInput(*m_service, 0u));
    EXPECT_FALSE(DecodeWin32FallbackCharInput(*m_service, 0xd83du));
    ASSERT_TRUE((codePoint = DecodeWin32FallbackCharInput(*m_service, 'B')));
    EXPECT_EQ(*codePoint, static_cast<u32>('B'));
    EXPECT_FALSE(DecodeWin32FallbackCharInput(*m_service, 0xde00u));
}

TEST_F(Win32TextInputFixture, SceneFallbackBridgeRejectsForeignServiceAndWrongThread){
    ASSERT_TRUE(m_service->end(m_token));
    auto unsupportedResult = CreateTextInputService(m_arena.arena, nullptr);
    ASSERT_TRUE(unsupportedResult);
    const GlobalUniquePtr<ITextInputService> unsupported = Move(*unsupportedResult);
    EXPECT_FALSE(DecodeWin32FallbackCharInput(*unsupported, 'A'));

    EXPECT_FALSE(ResetWin32FallbackCharInput(*unsupported));
    Thread worker([&](){
        EXPECT_FALSE(DecodeWin32FallbackCharInput(*m_service, 'A'));

        EXPECT_FALSE(ResetWin32FallbackCharInput(*m_service));
    });
    worker.join();
    Expected<u32> codePoint = DecodeWin32FallbackCharInput(*m_service, 'C');
    ASSERT_TRUE(codePoint);
    EXPECT_EQ(*codePoint, static_cast<u32>('C'));
}


TEST_F(Win32TextInputFixture, InsertCharCreatesProvisionalPreeditWithoutCommitAndHonorsNoMoveCaret){
    EXPECT_EQ(SendMessageW(m_window, WM_IME_STARTCOMPOSITION, 0u, 0), 0);
    EXPECT_EQ(SendMessageW(m_window, WM_IME_COMPOSITION, 0xd55cu, CS_INSERTCHAR | CS_NOMOVECARET), 0);
    TextInputEvent event(m_arena.arena);
    ASSERT_EQ(m_service->poll(m_token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.kind, TextInputEventKind::Preedit);
    EXPECT_EQ(event.text, "\xED\x95\x9C");
    EXPECT_EQ(event.anchorByte, 0u);
    EXPECT_EQ(event.caretByte, 0u);
    EXPECT_EQ(m_service->poll(m_token, event), TextInputPollResult::Pending);
    EXPECT_EQ(SendMessageW(m_window, WM_IME_COMPOSITION, 'A', CS_INSERTCHAR), 0);
    ASSERT_EQ(m_service->poll(m_token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.kind, TextInputEventKind::Preedit);
    EXPECT_EQ(event.text, "A\xED\x95\x9C");
    EXPECT_EQ(event.caretByte, 1u);
    EXPECT_EQ(SendMessageW(m_window, WM_IME_ENDCOMPOSITION, 0u, 0), 0);
    ASSERT_EQ(m_service->poll(m_token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.kind, TextInputEventKind::Preedit);
    EXPECT_TRUE(event.text.empty());
    EXPECT_EQ(m_service->poll(m_token, event), TextInputPollResult::Pending);
}

TEST_F(Win32TextInputFixture, InsertCharSupplementaryPairProducesOneUtf8PreeditAndCancellationClearsIt){
    EXPECT_EQ(SendMessageW(m_window, WM_IME_STARTCOMPOSITION, 0u, 0), 0);
    TextInputEvent event(m_arena.arena);
    ASSERT_EQ(m_service->poll(m_token, event), TextInputPollResult::Event);
    EXPECT_EQ(SendMessageW(m_window, WM_IME_COMPOSITION, 0xd83du, CS_INSERTCHAR), 0);
    EXPECT_EQ(m_service->poll(m_token, event), TextInputPollResult::Pending);
    EXPECT_EQ(SendMessageW(m_window, WM_IME_COMPOSITION, 0xde00u, CS_INSERTCHAR), 0);
    ASSERT_EQ(m_service->poll(m_token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.kind, TextInputEventKind::Preedit);
    EXPECT_EQ(event.text, "\xF0\x9F\x98\x80");
    EXPECT_EQ(event.caretByte, 4u);
    EXPECT_EQ(SendMessageW(m_window, WM_IME_COMPOSITION, 0u, 0), 0);
    ASSERT_EQ(m_service->poll(m_token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.kind, TextInputEventKind::Preedit);
    EXPECT_TRUE(event.text.empty());
    EXPECT_EQ(m_service->poll(m_token, event), TextInputPollResult::Pending);
}


TEST_F(Win32TextInputFixture, KeyboardFocusMessagesCancelQueuedWorkWithoutActivationChange){
    EXPECT_EQ(SendMessageW(m_window, WM_CHAR, 'A', 0), 0);
    EXPECT_EQ(SendMessageW(m_window, WM_IME_STARTCOMPOSITION, 0u, 0), 0);
    EXPECT_EQ(SendMessageW(m_window, WM_IME_COMPOSITION, 0xd55cu, CS_INSERTCHAR), 0);
    EXPECT_EQ(SendMessageW(m_window, WM_KILLFOCUS, 0u, 0), 0);
    EXPECT_FALSE(m_service->activeSession().valid());
    TextInputEvent event(m_arena.arena);
    ASSERT_EQ(m_service->poll(m_token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.kind, TextInputEventKind::Cancelled);
    EXPECT_EQ(event.cancelReason, TextInputCancelReason::FocusLost);
    EXPECT_TRUE(event.text.empty());
    EXPECT_EQ(m_service->poll(m_token, event), TextInputPollResult::InvalidSession);
    const auto unavailableBegin = m_service->begin({});
    ASSERT_FALSE(unavailableBegin);
    EXPECT_EQ(unavailableBegin.error(), TextInputAdmission::Unavailable);
    EXPECT_EQ(SendMessageW(m_window, WM_SETFOCUS, 0u, 0), 0);
    const auto tokenAdmission = m_service->begin({});
    ASSERT_TRUE(tokenAdmission);
    m_token = *tokenAdmission;
    ASSERT_TRUE(m_token.valid());
    EXPECT_EQ(SendMessageW(m_window, WM_CHAR, 'B', 0), 0);
    ASSERT_EQ(m_service->poll(m_token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.kind, TextInputEventKind::Commit);
    EXPECT_EQ(event.text, "B");
}

TEST_F(Win32TextInputFixture, InactiveKeyboardFocusMessagesResetSceneSurrogateState){
    ASSERT_TRUE(m_service->end(m_token));
    EXPECT_FALSE(DecodeWin32FallbackCharInput(*m_service, 0xd83du));
    EXPECT_EQ(SendMessageW(m_window, WM_KILLFOCUS, 0u, 0), 0);
    EXPECT_EQ(SendMessageW(m_window, WM_SETFOCUS, 0u, 0), 0);
    EXPECT_FALSE(DecodeWin32FallbackCharInput(*m_service, 0xde00u));
}


TEST_F(Win32TextInputFixture, UnfocusedSceneFallbackRefusesLateWindowCharacterAndClearsPartialPair){
    ASSERT_TRUE(m_service->end(m_token));
    EXPECT_EQ(SendMessageW(m_window, WM_CHAR, 0xd83du, 0), 0);
    EXPECT_EQ(m_sceneCharacterCount, 0u);
    // Direct focus state update leaves fallback reset to the refusal guard, as a defensive late-message case.
    ASSERT_TRUE(m_service->setFocused(false));
    EXPECT_EQ(SendMessageW(m_window, WM_CHAR, 'A', 0), 0);
    EXPECT_EQ(m_sceneCharacterCount, 0u);
    ASSERT_TRUE(m_service->setFocused(true));
    EXPECT_EQ(SendMessageW(m_window, WM_CHAR, 0xde00u, 0), 0);
    EXPECT_EQ(m_sceneCharacterCount, 0u);
    EXPECT_EQ(SendMessageW(m_window, WM_CHAR, 'B', 0), 0);
    EXPECT_EQ(m_sceneCharacterCount, 1u);
    EXPECT_EQ(m_sceneCodePoint, static_cast<u32>('B'));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#endif


#if defined(NWB_PLATFORM_LINUX)

#include <core/os/linux/pipe_io.h>

#include <tests/common/test_context.h>

#include <gtest/gtest.h>

#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <unistd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_pipe_io_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core;

struct OwnedPipe final : NoCopy{
    int m_readFd = -1;
    int m_writeFd = -1;


public:
    ~OwnedPipe()noexcept{
        CloseClipboardPipe(m_readFd);
        CloseClipboardPipe(m_writeFd);
    }


public:
    [[nodiscard]] b8 open()noexcept{
        const auto pipe = OpenClipboardPipe();
        if(!pipe)
            return false;
        m_readFd = pipe->readFd;
        m_writeFd = pipe->writeFd;
        return true;
    }

    [[nodiscard]] int releaseWrite()noexcept{
        const int fd = m_writeFd;
        m_writeFd = -1;
        return fd;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(ClipboardPipe, PartialUnicodeStaysPendingUntilValidEof){
    NWB::Tests::TestArena arena;
    OwnedPipe pipe;
    ASSERT_TRUE(pipe.open());
    ClipboardTextAccumulator received(arena.arena);
    Expected<b8, ClipboardPipeReadFailure> finished = MakeUnexpected(ClipboardPipeReadFailure{});
    ASSERT_EQ(write(pipe.m_writeFd, "\xED", 1u), 1);
    ASSERT_TRUE((finished = ReadClipboardPipe(pipe.m_readFd, received)));
    EXPECT_FALSE(*finished);
    ASSERT_EQ(write(pipe.m_writeFd, "\x95\x9C\xF0\x9F", 4u), 4);
    ASSERT_TRUE((finished = ReadClipboardPipe(pipe.m_readFd, received)));
    EXPECT_FALSE(*finished);
    ASSERT_EQ(write(pipe.m_writeFd, "\x98\x80", 2u), 2);
    CloseClipboardPipe(pipe.m_writeFd);
    ASSERT_TRUE((finished = ReadClipboardPipe(pipe.m_readFd, received)));
    EXPECT_TRUE(*finished);
    EXPECT_EQ(received.text(), "\xED\x95\x9C\xF0\x9F\x98\x80");
}

TEST(ClipboardPipe, TruncatedUnicodeIsRejectedAtEof){
    NWB::Tests::TestArena arena;
    OwnedPipe pipe;
    ASSERT_TRUE(pipe.open());
    ClipboardTextAccumulator received(arena.arena);
    ASSERT_EQ(write(pipe.m_writeFd, "\xF0\x9F", 2u), 2);
    CloseClipboardPipe(pipe.m_writeFd);
    Expected<b8, ClipboardPipeReadFailure> finished = MakeUnexpected(ClipboardPipeReadFailure{});
    finished = ReadClipboardPipe(pipe.m_readFd, received);
    ASSERT_FALSE(finished);
    EXPECT_EQ(finished.error().status, ClipboardStatus::InvalidText);
    EXPECT_TRUE(finished.error().finished);
}

TEST(ClipboardPipe, WriterOwnsLargeInputAndResumesAfterFullPipe){
    NWB::Tests::TestArena arena;
    OwnedPipe pipe;
    ASSERT_TRUE(pipe.open());
    AString<Alloc::GlobalArena> input(2u * 1024u * 1024u, 'x', arena.arena);
    const AString<Alloc::GlobalArena> expected = input;
    ClipboardPipeWriter writer(arena.arena);
    const int writeFd = pipe.releaseWrite();
    ASSERT_TRUE(writer.begin(writeFd, input));
    input.assign("caller changed bytes");
    const int writerFlags = fcntl(writeFd, F_GETFL);
    ASSERT_GE(writerFlags, 0);
    ASSERT_NE(writerFlags & O_NONBLOCK, 0);
    for(usize attempt = 0u; attempt < 64u; ++attempt)
        ASSERT_EQ(writer.advance(), ClipboardStatus::Success);
    ASSERT_FALSE(writer.finished());
    pollfd writable{ .fd = writeFd, .events = POLLOUT, .revents = 0 };
    ASSERT_EQ(poll(&writable, 1u, 0), 0);

    ClipboardTextAccumulator received(arena.arena);
    b8 finished = false;
    for(usize attempt = 0u; attempt < 4096u && !finished; ++attempt){
        const auto read = ReadClipboardPipe(pipe.m_readFd, received);
        ASSERT_TRUE(read);
        finished = *read;
        ASSERT_EQ(writer.advance(), ClipboardStatus::Success);
    }
    EXPECT_TRUE(finished);
    EXPECT_TRUE(writer.finished());
    EXPECT_EQ(received.text(), expected);
}

TEST(ClipboardPipe, ClosedReaderFailsWithoutSigpipeOrChangingThreadMask){
    NWB::Tests::TestArena arena;
    OwnedPipe pipe;
    ASSERT_TRUE(pipe.open());
    CloseClipboardPipe(pipe.m_readFd);
    sigset_t before;
    ASSERT_EQ(pthread_sigmask(SIG_BLOCK, nullptr, &before), 0);
    ClipboardPipeWriter writer(arena.arena);
    ASSERT_TRUE(writer.begin(pipe.releaseWrite(), "reader is gone"));
    EXPECT_EQ(writer.advance(), ClipboardStatus::NativeFailure);
    EXPECT_TRUE(writer.finished());
    sigset_t after;
    ASSERT_EQ(pthread_sigmask(SIG_BLOCK, nullptr, &after), 0);
    EXPECT_EQ(sigismember(&before, SIGPIPE), sigismember(&after, SIGPIPE));
}

TEST(ClipboardPipe, WriterCanBeReusedForEmptyAndShorterTransfers){
    NWB::Tests::TestArena arena;
    ClipboardPipeWriter writer(arena.arena);
    ClipboardTextAccumulator received(arena.arena);
    Expected<b8, ClipboardPipeReadFailure> finished = MakeUnexpected(ClipboardPipeReadFailure{});
    OwnedPipe first;
    ASSERT_TRUE(first.open());
    ASSERT_TRUE(writer.begin(first.releaseWrite(), "old long contents"));
    ASSERT_EQ(writer.advance(), ClipboardStatus::Success);
    ASSERT_TRUE(writer.finished());
    ASSERT_TRUE((finished = ReadClipboardPipe(first.m_readFd, received)));
    ASSERT_TRUE(*finished);
    EXPECT_EQ(received.text(), "old long contents");

    OwnedPipe empty;
    ASSERT_TRUE(empty.open());
    ASSERT_TRUE(writer.begin(empty.releaseWrite(), {}));
    ASSERT_EQ(writer.advance(), ClipboardStatus::Success);
    ASSERT_TRUE(writer.finished());
    received.clear();
    ASSERT_TRUE((finished = ReadClipboardPipe(empty.m_readFd, received)));
    EXPECT_TRUE(*finished);
    EXPECT_TRUE(received.text().empty());

    OwnedPipe last;
    ASSERT_TRUE(last.open());
    ASSERT_TRUE(writer.begin(last.releaseWrite(), "new"));
    ASSERT_EQ(writer.advance(), ClipboardStatus::Success);
    ASSERT_TRUE(writer.finished());
    received.clear();
    ASSERT_TRUE((finished = ReadClipboardPipe(last.m_readFd, received)));
    EXPECT_TRUE(*finished);
    EXPECT_EQ(received.text(), "new");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#endif


#if defined(NWB_PLATFORM_LINUX)

// Include gtest before X11 headers because X11 defines None as a macro.
#include <tests/common/test_context.h>

#include <core/os/linux/x11/clipboard.h>
#include <core/os/linux/x11/x11_checked.h>

#include <global/environment.h>
#include <global/thread.h>

#include <gtest/gtest.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>

#ifdef Success
#undef Success
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_x11_clipboard_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_SelectionChangeTimeoutMS = 3000u;
constexpr u32 s_ClipboardCompletionTimeoutMS = 6000u;
constexpr u32 s_PollIntervalMS = 1u;
constexpr AStringView s_ClipboardAtomName = "CLIPBOARD";

using namespace NWB::Core;

class X11ClipboardFixture : public testing::Test{
protected:
    virtual void SetUp()override{
        const auto isolated = EnvironmentVariableEquals(m_arena.arena, "NWB_X11_CLIPBOARD_TEST_ISOLATED", "1");
        ASSERT_TRUE(isolated);
        if(!*isolated)
            GTEST_SKIP() << "Native clipboard tests require the isolated Xvfb CTest entry";
        for(usize index = 0u; index < m_displays.size(); ++index){
            m_displays[index] = XOpenDisplay(nullptr);
            ASSERT_NE(m_displays[index], nullptr);
            auto serviceResult = CreateX11ClipboardService(m_arena.arena, *m_displays[index]);
            ASSERT_TRUE(serviceResult);
            m_services[index] = Move(*serviceResult);
            ASSERT_TRUE(m_services[index]);
        }
    }

    virtual void TearDown()override{
        for(auto& service : m_services)
            service.reset();
        for(Display*& display : m_displays){
            if(display){
                EXPECT_EQ(XCloseDisplay(display), 0);
                display = nullptr;
            }
        }
    }

    [[nodiscard]] b8 pumpAll(){
        for(usize index = 0u; index < m_displays.size(); ++index){
            if(!m_services[index]->pump())
                return false;
            usize dispatched = 0u;
            while(XPending(m_displays[index]) > 0 && dispatched < 256u){
                XEvent event{};
                if(XNextEvent(m_displays[index], &event) != 0)
                    return false;
                if(event.type == SelectionRequest)
                    m_sawSelectionRequest = true;
                if(DispatchX11ClipboardEvent(*m_services[index], event)){
                    ++dispatched;
                    continue;
                }
                ++dispatched;
            }
        }
        return true;
    }

    [[nodiscard]] b8 awaitCompletion(const ClipboardRequestToken token, IClipboardService& service, ClipboardCompletion& completion){
        const Timer deadline = TimerAddMS(TimerNow(), __hidden_x11_clipboard_tests::s_ClipboardCompletionTimeoutMS);
        while(TimerNow() < deadline){
            if(!pumpAll())
                return false;
            const ClipboardPollResult::Enum result = service.poll(token, completion);
            if(result == ClipboardPollResult::Completed)
                return true;
            if(result != ClipboardPollResult::Pending)
                return false;
            SleepMS(__hidden_x11_clipboard_tests::s_PollIntervalMS);
        }
        return false;
    }

    [[nodiscard]] b8 writeText(const usize index, const ClipboardChannel::Enum channel, const AStringView text){
        ClipboardCompletion completion(m_arena.arena);
        const auto request = m_services[index]->requestWriteText(channel, text);
        return request && request->valid() && awaitCompletion(*request, *m_services[index], completion) && completion.status == ClipboardStatus::Success;
    }


protected:
    NWB::Tests::TestArena<> m_arena;
    Array<Display*, 2u> m_displays{};
    Array<GlobalUniquePtr<IClipboardService>, 2u> m_services;
    b8 m_sawSelectionRequest = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(X11ClipboardFixture, PropertyAbsenceAndRetiredWindowPreserveDifferentNativeResults){
    Window retired = 0u;
    {
        X11CheckedOperation created(*m_displays[0]);
        retired = XCreateSimpleWindow(m_displays[0], DefaultRootWindow(m_displays[0]), 0, 0, 1, 1, 0, 0, 0);
        ASSERT_TRUE(created.succeeded());
        ASSERT_NE(retired, 0u);
    }
    {
        const auto absent = ReadX11Property(*m_displays[0], retired, XA_STRING, false, 16u);
        ASSERT_TRUE(absent);
        EXPECT_EQ(absent->type, 0u);
        EXPECT_EQ(absent->count, 0u);
    }
    {
        X11CheckedOperation destroyed(*m_displays[0]);
        XDestroyWindow(m_displays[0], retired);
        ASSERT_TRUE(destroyed.succeeded());
    }
    const auto failed = ReadX11Property(*m_displays[0], retired, XA_STRING, false, 16u);
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().errorCode, static_cast<u8>(BadWindow));
}

TEST_F(X11ClipboardFixture, WritingPrimarySelectionDoesNotOverwriteClipboardUtf8){
    const AStringView clipboard = "clipboard \xec\x95\x88\xeb\x85\x95 \xf0\x9f\x99\x82";
    const AStringView primary = "primary caf\xc3\xa9";
    ASSERT_TRUE(m_services[0]->capabilities(ClipboardChannel::PrimarySelection).readText);
    ASSERT_TRUE(writeText(0u, ClipboardChannel::Clipboard, clipboard));
    ASSERT_TRUE(writeText(0u, ClipboardChannel::PrimarySelection, primary));
    ClipboardCompletion completion(m_arena.arena);
    const auto clipTokenAdmission = m_services[1]->requestReadText(ClipboardChannel::Clipboard);
    ASSERT_TRUE(clipTokenAdmission);
    const ClipboardRequestToken clipToken = *clipTokenAdmission;
    ASSERT_TRUE(awaitCompletion(clipToken, *m_services[1], completion));
    ASSERT_EQ(completion.status, ClipboardStatus::Success);
    EXPECT_EQ(completion.text, clipboard);
    const auto primaryTokenAdmission = m_services[1]->requestReadText(ClipboardChannel::PrimarySelection);
    ASSERT_TRUE(primaryTokenAdmission);
    const ClipboardRequestToken primaryToken = *primaryTokenAdmission;
    ASSERT_TRUE(awaitCompletion(primaryToken, *m_services[1], completion));
    ASSERT_EQ(completion.status, ClipboardStatus::Success);
    EXPECT_EQ(completion.text, primary);
}

TEST_F(X11ClipboardFixture, IncrementalReadRetainsSnapshotAfterOwnerLosesSelection){
    AString<Alloc::GlobalArena> original(200000u, 'x', m_arena.arena);
    original.replace(65534u, 1u, "\xf0\x9f\x99\x82");
    ASSERT_TRUE(writeText(0u, ClipboardChannel::Clipboard, original));
    m_sawSelectionRequest = false;
    ClipboardCompletion completion(m_arena.arena);
    const auto tokenAdmission = m_services[1]->requestReadText(ClipboardChannel::Clipboard);
    ASSERT_TRUE(tokenAdmission);
    const ClipboardRequestToken token = *tokenAdmission;
    const Timer deadline = TimerAddMS(TimerNow(), __hidden_x11_clipboard_tests::s_SelectionChangeTimeoutMS);
    while(!m_sawSelectionRequest && TimerNow() < deadline){
        ASSERT_TRUE(pumpAll());
        SleepMS(__hidden_x11_clipboard_tests::s_PollIntervalMS);
    }
    ASSERT_TRUE(m_sawSelectionRequest);
    const Window replacement = XCreateSimpleWindow(m_displays[1], DefaultRootWindow(m_displays[1]), 0, 0, 1, 1, 0, 0, 0);
    ASSERT_NE(replacement, 0u);
    const Atom clipboard = XInternAtom(m_displays[1], s_ClipboardAtomName.data(), False);
    XSetSelectionOwner(m_displays[1], clipboard, replacement, CurrentTime);
    XSync(m_displays[1], False);
    ASSERT_EQ(XGetSelectionOwner(m_displays[1], clipboard), replacement);
    ASSERT_TRUE(awaitCompletion(token, *m_services[1], completion));
    EXPECT_EQ(completion.status, ClipboardStatus::Success);
    EXPECT_EQ(completion.text, original);
    XDestroyWindow(m_displays[1], replacement);
    XSync(m_displays[1], False);
}

TEST_F(X11ClipboardFixture, CancelledRequestWindowCannotCompleteFreshRequest){
    ASSERT_TRUE(writeText(0u, ClipboardChannel::Clipboard, "first"));
    const auto cancelledAdmission = m_services[1]->requestReadText(ClipboardChannel::Clipboard);
    ASSERT_TRUE(cancelledAdmission);
    const ClipboardRequestToken cancelled = *cancelledAdmission;
    ASSERT_TRUE(m_services[1]->pump());
    ASSERT_TRUE(m_services[1]->cancel(cancelled));
    ASSERT_TRUE(writeText(0u, ClipboardChannel::Clipboard, "after cancellation"));
    ClipboardCompletion completion(m_arena.arena);
    const auto freshAdmission = m_services[1]->requestReadText(ClipboardChannel::Clipboard);
    ASSERT_TRUE(freshAdmission);
    const ClipboardRequestToken fresh = *freshAdmission;
    ASSERT_TRUE(awaitCompletion(fresh, *m_services[1], completion));
    EXPECT_EQ(completion.status, ClipboardStatus::Success);
    EXPECT_EQ(completion.text, "after cancellation");
    EXPECT_EQ(m_services[1]->poll(cancelled, completion), ClipboardPollResult::InvalidRequest);
}

TEST_F(X11ClipboardFixture, QueuedOwnershipLossCannotClearNewlyReacquiredText){
    ASSERT_TRUE(writeText(0u, ClipboardChannel::Clipboard, "before ownership loss"));
    const Atom clipboard = XInternAtom(m_displays[1], s_ClipboardAtomName.data(), False);
    const Window temporaryOwner = XCreateSimpleWindow(m_displays[1], DefaultRootWindow(m_displays[1]), 0, 0, 1, 1, 0, 0, 0);
    ASSERT_NE(temporaryOwner, 0u);
    XSetSelectionOwner(m_displays[1], clipboard, temporaryOwner, CurrentTime);
    XSync(m_displays[1], False);
    ASSERT_EQ(XGetSelectionOwner(m_displays[1], clipboard), temporaryOwner);

    // Retain the genuine loss event until the new ownership is established.
    XEvent ownershipLoss{};
    b8 queuedLoss = false;
    const Timer lossDeadline = TimerAddMS(TimerNow(), __hidden_x11_clipboard_tests::s_SelectionChangeTimeoutMS);
    while(!queuedLoss && TimerNow() < lossDeadline){
        queuedLoss = XCheckTypedEvent(m_displays[0], SelectionClear, &ownershipLoss) != False;
        if(!queuedLoss)
            SleepMS(__hidden_x11_clipboard_tests::s_PollIntervalMS);
    }
    ASSERT_TRUE(queuedLoss);
    ASSERT_EQ(ownershipLoss.xselectionclear.selection, clipboard);

    ClipboardCompletion completion(m_arena.arena);
    const auto writeAdmission = m_services[0]->requestWriteText(ClipboardChannel::Clipboard, "after reacquisition");
    ASSERT_TRUE(writeAdmission);
    const ClipboardRequestToken write = *writeAdmission;
    ASSERT_TRUE(write.valid());
    ASSERT_TRUE(awaitCompletion(write, *m_services[0], completion));
    ASSERT_EQ(completion.status, ClipboardStatus::Success);
    ASSERT_NE(XGetSelectionOwner(m_displays[1], clipboard), temporaryOwner);
    ASSERT_TRUE(DispatchX11ClipboardEvent(*m_services[0], ownershipLoss));
    ASSERT_TRUE(pumpAll());
    const auto readAdmission = m_services[1]->requestReadText(ClipboardChannel::Clipboard);
    ASSERT_TRUE(readAdmission);
    const ClipboardRequestToken read = *readAdmission;
    ASSERT_TRUE(awaitCompletion(read, *m_services[1], completion));
    EXPECT_EQ(completion.status, ClipboardStatus::Success);
    EXPECT_EQ(completion.text, "after reacquisition");
    XDestroyWindow(m_displays[1], temporaryOwner);
    XSync(m_displays[1], False);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#endif


