// limztudio@gmail.com
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
        , text(arena)
    {}


public:
    [[nodiscard]] virtual ClipboardCapabilities capabilities(const ClipboardChannel::Enum channel)const noexcept override{
        return channel == ClipboardChannel::Clipboard ? ClipboardCapabilities{ true, true } : ClipboardCapabilities{};
    }


protected:
    virtual void startNativeRequest(const ClipboardRequestToken token, const ClipboardOperation::Enum operation, const ClipboardChannel::Enum, const AStringView input)override{
        m_nativeToken = token;
        if(operation == ClipboardOperation::ReadText){
            ++readCount;
            if(onRead)
                onRead();
            if(m_nativeToken != token)
                return;
            m_nativeToken = {};
            if(!completeNativeRequest(token, readStatus, text))
                NWB_FATAL_ASSERT(false);
        }
        else{
            ++writeCount;
            text.assign(input.data(), input.size());
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
    AString<Alloc::GlobalArena> text;
    Function<void()> onRead;
    ClipboardStatus::Enum readStatus = ClipboardStatus::Success;
    usize readCount = 0u;
    usize writeCount = 0u;


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
    EXPECT_EQ(service.writeCount, 0u);
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
    EXPECT_EQ(service.writeCount, 1u);
    EXPECT_EQ(service.text, "keep");
    ASSERT_TRUE(service.cancel(current));
    EXPECT_EQ(service.poll(current, completion), ClipboardPollResult::InvalidRequest);
    EXPECT_EQ(service.text, "keep");
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
    service.onRead = [&](){
        EXPECT_TRUE(service.cancel(old));
        const auto currentAdmission = service.requestReadText(ClipboardChannel::Clipboard);
        ASSERT_TRUE(currentAdmission);
        current = *currentAdmission;
        EXPECT_FALSE(service.pump());
    };
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(service.pump()));
    EXPECT_EQ(service.poll(old, completion), ClipboardPollResult::InvalidRequest);
    EXPECT_EQ(service.poll(current, completion), ClipboardPollResult::Pending);
    service.onRead = {};
    service.text.assign("new completion");
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
    EXPECT_EQ(service.readCount, 0u);
    service.readStatus = ClipboardStatus::Unavailable;
    service.text.assign("must not escape failed read");
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
    EXPECT_EQ(service.writeCount, 0u);
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
    }
    );
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
        service.text.assign("owned result");
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

