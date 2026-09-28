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
    [[nodiscard]] virtual ClipboardStatus::Enum readNativeText(ClipboardChannel::Enum, AString<Alloc::GlobalArena>& output)override{
        ++readCount;
        if(onRead)
            onRead();
        output = text;
        return readStatus;
    }

    [[nodiscard]] virtual ClipboardStatus::Enum writeNativeText(ClipboardChannel::Enum, const AStringView input)override{
        ++writeCount;
        text.assign(input.data(), input.size());
        return ClipboardStatus::Success;
    }


public:
    AString<Alloc::GlobalArena> text;
    Function<void()> onRead;
    ClipboardStatus::Enum readStatus = ClipboardStatus::Success;
    usize readCount = 0u;
    usize writeCount = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Clipboard, RequestsCopyTextAndPublishOnlyWhenPumped){
    NWB::Tests::TestArena arena;
    FakeClipboardService service(arena.arena);
    ClipboardCompletion completion(arena.arena);
    AString<Alloc::GlobalArena> input("Hello \xED\x95\x9C\xEA\xB8\x80 \xF0\x9F\x98\x80", arena.arena);
    const AString<Alloc::GlobalArena> original = input;
    const ClipboardRequestResult write = service.requestWriteText(ClipboardChannel::Clipboard, input);
    ASSERT_EQ(write.admission, ClipboardAdmission::Accepted);
    input.assign("changed");
    EXPECT_EQ(service.poll(write.token, completion), ClipboardPollResult::Pending);
    EXPECT_EQ(service.writeCount, 0u);
    const ClipboardRequestResult read = service.requestReadText(ClipboardChannel::Clipboard);
    ASSERT_TRUE(service.pump());
    EXPECT_EQ(service.poll(write.token, completion), ClipboardPollResult::Completed);
    EXPECT_EQ(completion.operation, ClipboardOperation::WriteText);
    EXPECT_EQ(completion.status, ClipboardStatus::Success);
    EXPECT_TRUE(completion.text.empty());
    EXPECT_EQ(service.poll(read.token, completion), ClipboardPollResult::Completed);
    EXPECT_EQ(completion.text, original);
    EXPECT_EQ(completion.token, read.token);
    EXPECT_EQ(service.poll(read.token, completion), ClipboardPollResult::InvalidRequest);
}

TEST(Clipboard, CancelPreventsPendingWritesAndRejectsReusedGenerations){
    NWB::Tests::TestArena arena;
    FakeClipboardService service(arena.arena);
    ClipboardCompletion completion(arena.arena);
    const ClipboardRequestToken canceled = service.requestWriteText(ClipboardChannel::Clipboard, "discard").token;
    ASSERT_TRUE(service.cancel(canceled));
    const ClipboardRequestToken current = service.requestWriteText(ClipboardChannel::Clipboard, "keep").token;
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
    const ClipboardRequestToken first = service.requestReadText(ClipboardChannel::Clipboard).token;
    const ClipboardRequestToken second = service.requestWriteText(ClipboardChannel::Clipboard, "second").token;
    ASSERT_TRUE(service.cancel(first));
    const ClipboardRequestToken third = service.requestReadText(ClipboardChannel::Clipboard).token;
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
        old = previous.requestReadText(ClipboardChannel::Clipboard).token;
    }
    FakeClipboardService service(arena.arena);
    const ClipboardRequestToken current = service.requestReadText(ClipboardChannel::Clipboard).token;
    EXPECT_NE(old.service, current.service);
    EXPECT_EQ(service.poll(old, completion), ClipboardPollResult::InvalidRequest);
    EXPECT_FALSE(service.cancel(old));
    EXPECT_EQ(service.poll({}, completion), ClipboardPollResult::InvalidRequest);
}

TEST(Clipboard, CanceledNativeCompletionCannotReachNewRequest){
    NWB::Tests::TestArena arena;
    FakeClipboardService service(arena.arena);
    ClipboardCompletion completion(arena.arena);
    const ClipboardRequestToken old = service.requestReadText(ClipboardChannel::Clipboard).token;
    ClipboardRequestToken current;
    service.onRead = [&](){
        EXPECT_TRUE(service.cancel(old));
        current = service.requestReadText(ClipboardChannel::Clipboard).token;
        EXPECT_FALSE(service.pump());
    };
    ASSERT_TRUE(service.pump());
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
    const ClipboardRequestToken primary = service.requestReadText(ClipboardChannel::PrimarySelection).token;
    ASSERT_TRUE(service.pump());
    EXPECT_EQ(service.poll(primary, completion), ClipboardPollResult::Completed);
    EXPECT_EQ(completion.status, ClipboardStatus::Unsupported);
    EXPECT_EQ(service.readCount, 0u);
    service.readStatus = ClipboardStatus::Unavailable;
    service.text.assign("must not escape failed read");
    const ClipboardRequestToken unavailable = service.requestReadText(ClipboardChannel::Clipboard).token;
    ASSERT_TRUE(service.pump());
    EXPECT_EQ(service.poll(unavailable, completion), ClipboardPollResult::Completed);
    EXPECT_EQ(completion.status, ClipboardStatus::Unavailable);
    EXPECT_TRUE(completion.text.empty());
}

TEST(Clipboard, UnsupportedFactoryDoesNotAccessNativeClipboard){
    NWB::Tests::TestArena arena;
    GlobalUniquePtr<IClipboardService> service = CreateClipboardService(arena.arena, nullptr);
    ASSERT_TRUE(service);
    EXPECT_TRUE(service->isOwnerThread());
    EXPECT_FALSE(service->capabilities(ClipboardChannel::Clipboard).readText);
    EXPECT_FALSE(service->capabilities(ClipboardChannel::Clipboard).writeText);
    EXPECT_FALSE(service->capabilities(ClipboardChannel::PrimarySelection).readText);
    const ClipboardRequestToken token = service->requestWriteText(ClipboardChannel::Clipboard, "unused").token;
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
        const ClipboardRequestResult request = service.requestReadText(ClipboardChannel::Clipboard);
        ASSERT_EQ(request.admission, ClipboardAdmission::Accepted);
        token = request.token;
    }
    EXPECT_EQ(service.requestReadText(ClipboardChannel::Clipboard).admission, ClipboardAdmission::QueueFull);
    for(const ClipboardRequestToken token : tokens)
        ASSERT_TRUE(service.cancel(token));
    const ClipboardRequestToken invalid = service.requestWriteText(ClipboardChannel::Clipboard, AStringView("a\0b", 3u)).token;
    ClipboardCompletion completion(arena.arena);
    EXPECT_EQ(service.poll(invalid, completion), ClipboardPollResult::Completed);
    EXPECT_EQ(completion.status, ClipboardStatus::InvalidText);
    AString<Alloc::GlobalArena> oversized(s_ClipboardMaxTextBytes + 1u, 'x', arena.arena);
    const ClipboardRequestToken large = service.requestWriteText(ClipboardChannel::Clipboard, oversized).token;
    EXPECT_EQ(service.poll(large, completion), ClipboardPollResult::Completed);
    EXPECT_EQ(completion.status, ClipboardStatus::TooLarge);
    EXPECT_EQ(service.writeCount, 0u);
}

TEST(Clipboard, WrongThreadCallsNeverTouchRequestStorage){
    NWB::Tests::TestArena arena;
    FakeClipboardService service(arena.arena);
    ClipboardCompletion completion(arena.arena);
    const ClipboardRequestToken token = service.requestReadText(ClipboardChannel::Clipboard).token;
    Thread worker([&](){
        EXPECT_FALSE(service.isOwnerThread());
        EXPECT_EQ(service.requestReadText(ClipboardChannel::Clipboard).admission, ClipboardAdmission::WrongThread);
        EXPECT_EQ(service.requestWriteText(ClipboardChannel::Clipboard, "ignored").admission, ClipboardAdmission::WrongThread);
        EXPECT_EQ(service.poll(token, completion), ClipboardPollResult::WrongThread);
        EXPECT_FALSE(service.cancel(token));
        EXPECT_FALSE(service.pump());
    });
    worker.join();
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
        const ClipboardRequestToken token = service.requestReadText(ClipboardChannel::Clipboard).token;
        ASSERT_TRUE(service.pump());
        EXPECT_EQ(service.poll(token, completion), ClipboardPollResult::Completed);
    }
    EXPECT_EQ(completion.text, "owned result");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

