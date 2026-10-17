// limztudio@gmail.com
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

    [[nodiscard]] bool deliver(const ClipboardRequestToken token, const ClipboardStatus::Enum status, const AStringView text = {}){
        const bool completed = completeNativeRequest(token, status, text);
        if(completed && token == m_nativeToken)
            m_nativeToken = {};
        return completed;
    }


protected:
    virtual void startNativeRequest(const ClipboardRequestToken token, const ClipboardOperation::Enum operation, const ClipboardChannel::Enum channel, const AStringView text)override{
        static_cast<void>(channel);
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
    bool observedCancellation = false;
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

