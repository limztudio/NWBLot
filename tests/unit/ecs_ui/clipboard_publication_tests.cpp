// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/clipboard_publications.h>

#include <core/os/clipboard_service.h>
#include <tests/common/test_context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_clipboard_publication_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Core;
using namespace NWB::Impl;

class DeferredPublications final : public QueuedClipboardService{
public:
    explicit DeferredPublications(Alloc::GlobalArena& arena)
        : QueuedClipboardService(arena)
        , startedText(arena)
    {}


public:
    [[nodiscard]] virtual ClipboardCapabilities capabilities(const ClipboardChannel::Enum channel)const noexcept override{
        return channel == ClipboardChannel::Clipboard ? ClipboardCapabilities{ true, true } : ClipboardCapabilities{};
    }

    [[nodiscard]] bool deliver(const ClipboardRequestToken token, const ClipboardStatus::Enum status, const AStringView text = {}){
        return completeNativeRequest(token, status, text);
    }


protected:
    virtual void startNativeRequest(
        const ClipboardRequestToken token, const ClipboardOperation::Enum operation,
        ClipboardChannel::Enum channel, const AStringView text)override{
        static_cast<void>(channel);
        startedToken = token;
        startedOperation = operation;
        startedText.assign(text.data(), text.size());
    }


public:
    AString<Alloc::GlobalArena> startedText;
    ClipboardRequestToken startedToken;
    ClipboardOperation::Enum startedOperation = ClipboardOperation::ReadText;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(UiClipboardPublications, CopiedWritePreservesNativeFifoBeforeLaterPasteRead){
    Tests::TestArena arena;
    DeferredPublications service(arena.arena);
    UiClipboardPublications publications(arena.arena, service);
    AString<Alloc::GlobalArena> source("copied", arena.arena);
    ASSERT_EQ(publications.request(source), UiClipboardPublicationStatus::Pending);
    source.assign("changed");
    const auto read = service.requestReadText(ClipboardChannel::Clipboard);
    ASSERT_EQ(read.admission, ClipboardAdmission::Accepted);
    ASSERT_TRUE(service.pump());
    EXPECT_EQ(service.startedOperation, ClipboardOperation::WriteText);
    EXPECT_EQ(service.startedText, "copied");
    EXPECT_EQ(publications.drain().status, UiClipboardPublicationStatus::Pending);
    ASSERT_TRUE(service.deliver(service.startedToken, ClipboardStatus::Success));
    ASSERT_TRUE(service.pump());
    EXPECT_EQ(service.startedOperation, ClipboardOperation::ReadText);
    EXPECT_EQ(service.startedToken, read.token);
    ASSERT_TRUE(service.deliver(read.token, ClipboardStatus::Success, "copied"));
    const auto result = publications.drain();
    EXPECT_EQ(result.status, UiClipboardPublicationStatus::Published);
    EXPECT_EQ(result.published, 1u);
    EXPECT_EQ(publications.pending(), 0u);
    ClipboardCompletion completion(arena.arena);
    ASSERT_EQ(service.poll(read.token, completion), ClipboardPollResult::Completed);
    EXPECT_EQ(completion.text, "copied");
}

TEST(UiClipboardPublications, BoundedQueueAndExplicitResetReleaseAllPendingNativeTokens){
    Tests::TestArena arena;
    DeferredPublications service(arena.arena);
    UiClipboardPublications publications(arena.arena, service);
    for(usize index = 0u; index < s_UiClipboardMaxPublications; ++index)
        ASSERT_EQ(publications.request("copy"), UiClipboardPublicationStatus::Pending);
    EXPECT_EQ(publications.pending(), s_UiClipboardMaxPublications);
    EXPECT_EQ(publications.request("overflow"), UiClipboardPublicationStatus::QueueFull);
    ASSERT_TRUE(service.pump());
    const auto old = service.startedToken;
    ASSERT_TRUE(publications.cancel());
    EXPECT_EQ(publications.pending(), 0u);
    EXPECT_FALSE(service.deliver(old, ClipboardStatus::Success));
    EXPECT_EQ(publications.drain().status, UiClipboardPublicationStatus::Idle);
    EXPECT_EQ(publications.request("new"), UiClipboardPublicationStatus::Pending);
}

TEST(UiClipboardPublications, CompletionFailuresAreReportedAndConsumedWithoutLosingSuccessfulPublications){
    Tests::TestArena arena;
    DeferredPublications service(arena.arena);
    UiClipboardPublications publications(arena.arena, service);
    ASSERT_EQ(publications.request("success"), UiClipboardPublicationStatus::Pending);
    ASSERT_EQ(publications.request("failure"), UiClipboardPublicationStatus::Pending);
    ASSERT_EQ(publications.request("unsupported"), UiClipboardPublicationStatus::Pending);
    ASSERT_TRUE(service.pump());
    ASSERT_TRUE(service.deliver(service.startedToken, ClipboardStatus::Success));
    ASSERT_TRUE(service.pump());
    ASSERT_TRUE(service.deliver(service.startedToken, ClipboardStatus::NativeFailure));
    ASSERT_TRUE(service.pump());
    ASSERT_TRUE(service.deliver(service.startedToken, ClipboardStatus::Unsupported));
    const auto result = publications.drain();
    EXPECT_EQ(result.published, 1u);
    EXPECT_EQ(result.failed, 2u);
    EXPECT_EQ(result.status, UiClipboardPublicationStatus::Unsupported);
    EXPECT_EQ(publications.pending(), 0u);
    EXPECT_EQ(publications.drain().failed, 0u);
}

TEST(UiClipboardPublications, InvalidInputAndUnsupportedChannelNeverAdmitNativeWork){
    Tests::TestArena arena;
    DeferredPublications service(arena.arena);
    UiClipboardPublications publications(arena.arena, service);
    EXPECT_EQ(publications.request("\xC0\xAF"), UiClipboardPublicationStatus::InvalidText);
    EXPECT_EQ(publications.request(AStringView("a\0b", 3u)), UiClipboardPublicationStatus::InvalidText);
    EXPECT_EQ(publications.request("primary", ClipboardChannel::PrimarySelection), UiClipboardPublicationStatus::Unsupported);
    EXPECT_EQ(publications.pending(), 0u);
    ASSERT_TRUE(service.pump());
    EXPECT_FALSE(service.startedToken.valid());
}

TEST(UiClipboardPublications, AnotherThreadCannotRequestDrainOrCancelOwnedPublication){
    Tests::TestArena arena;
    DeferredPublications service(arena.arena);
    UiClipboardPublications publications(arena.arena, service);
    ASSERT_EQ(publications.request("copy"), UiClipboardPublicationStatus::Pending);
    UiClipboardPublicationStatus::Enum requested = UiClipboardPublicationStatus::Idle;
    UiClipboardPublicationStatus::Enum drained = UiClipboardPublicationStatus::Idle;
    bool cancelled = true;
    Thread other([&](){
        requested = publications.request("other");
        drained = publications.drain().status;
        cancelled = publications.cancel();
    });
    other.join();
    EXPECT_EQ(requested, UiClipboardPublicationStatus::WrongThread);
    EXPECT_EQ(drained, UiClipboardPublicationStatus::WrongThread);
    EXPECT_FALSE(cancelled);
    EXPECT_EQ(publications.pending(), 1u);
    EXPECT_TRUE(publications.cancel());
}

TEST(UiClipboardPublications, DestructionCancelsUndeliveredWorkBeforeServiceRetirement){
    Tests::TestArena arena;
    DeferredPublications service(arena.arena);
    ClipboardRequestToken old;
    {
        UiClipboardPublications publications(arena.arena, service);
        ASSERT_EQ(publications.request("copy"), UiClipboardPublicationStatus::Pending);
        ASSERT_TRUE(service.pump());
        old = service.startedToken;
    }
    EXPECT_FALSE(service.deliver(old, ClipboardStatus::Success));
    ClipboardCompletion completion(arena.arena);
    EXPECT_EQ(service.poll(old, completion), ClipboardPollResult::InvalidRequest);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

