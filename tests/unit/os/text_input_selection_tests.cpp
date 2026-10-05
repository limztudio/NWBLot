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
    explicit SelectionTextInput(Alloc::GlobalArena& arena)
        : QueuedTextInputService(arena)
    {}


public:
    using QueuedTextInputService::emitCommit;
    using QueuedTextInputService::emitPreedit;
    using QueuedTextInputService::emitDeleteSurrounding;
    [[nodiscard]] virtual TextInputCapabilities capabilities()const noexcept override{ return { true, true, true, true }; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(TextInputSelection, ReverseSelectionHasTheSameOuterByteLimits){
    NWB::Tests::TestArena arena;
    SelectionTextInput service(arena.arena);
    ASSERT_TRUE(service.setFocused(true));
    const auto begun = service.begin({ {}, "abcDEFghi", 6u, 3u });
    ASSERT_EQ(begun.admission, TextInputAdmission::Accepted);
    const u64 revision = service.surroundingRevision(begun.token);
    EXPECT_EQ(service.emitDeleteSurrounding(begun.token, 4u, 0u, revision, TextInputDeletionBasis::Selection),
        TextInputAdmission::InvalidRange);
    EXPECT_EQ(service.emitDeleteSurrounding(begun.token, 0u, 4u, revision, TextInputDeletionBasis::Selection),
        TextInputAdmission::InvalidRange);
    ASSERT_EQ(service.emitDeleteSurrounding(begun.token, 3u, 3u, revision, TextInputDeletionBasis::Selection),
        TextInputAdmission::Accepted);
    TextInputEvent event(arena.arena);
    ASSERT_EQ(service.poll(begun.token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.deleteBeforeBytes, 3u);
    EXPECT_EQ(event.deleteAfterBytes, 3u);
    EXPECT_EQ(event.deletionBasis, TextInputDeletionBasis::Selection);
    EXPECT_EQ(service.poll(begun.token, event), TextInputPollResult::Pending);
}

TEST(TextInputSelection, UnknownBasisAndMissingOrStaleRevisionDoNotQueueAnyEvent){
    NWB::Tests::TestArena arena;
    SelectionTextInput service(arena.arena);
    ASSERT_TRUE(service.setFocused(true));
    const auto begun = service.begin({ {}, "abcDEFghi", 3u, 6u });
    ASSERT_EQ(begun.admission, TextInputAdmission::Accepted);
    const u64 revision = service.surroundingRevision(begun.token);
    ASSERT_EQ(service.updateSurrounding(begun.token, "abcDEFghi", 3u, 6u), TextInputAdmission::Accepted);
    const u64 currentRevision = service.surroundingRevision(begun.token);
    EXPECT_EQ(service.emitDeleteSurrounding(begun.token, 2u, 1u, revision, TextInputDeletionBasis::Selection),
        TextInputAdmission::InvalidRange);
    EXPECT_EQ(service.emitDeleteSurrounding(begun.token, 2u, 1u, 0u, TextInputDeletionBasis::Selection),
        TextInputAdmission::InvalidRange);
    constexpr TextInputDeletionBasis::Enum s_InvalidBasis = static_cast<TextInputDeletionBasis::Enum>(255u);
    EXPECT_EQ(service.emitDeleteSurrounding(begun.token, 0u, 0u, currentRevision, s_InvalidBasis), TextInputAdmission::InvalidRange);
    TextInputEvent event(arena.arena);
    EXPECT_EQ(service.poll(begun.token, event), TextInputPollResult::Pending);
    EXPECT_EQ(service.activeSession(), begun.token);
    ASSERT_EQ(service.emitDeleteSurrounding(begun.token, 2u, 1u, currentRevision, TextInputDeletionBasis::Selection),
        TextInputAdmission::Accepted);
    ASSERT_EQ(service.poll(begun.token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.surroundingRevision, currentRevision);
    EXPECT_EQ(event.deletionBasis, TextInputDeletionBasis::Selection);
    EXPECT_EQ(event.deleteBeforeBytes, 2u);
    EXPECT_EQ(event.deleteAfterBytes, 1u);
    EXPECT_EQ(service.poll(begun.token, event), TextInputPollResult::Pending);
}

TEST(TextInputSelection, OuterUtf8EndpointsMustRemainScalarBoundaries){
    NWB::Tests::TestArena arena;
    SelectionTextInput service(arena.arena);
    ASSERT_TRUE(service.setFocused(true));
    const auto begun = service.begin({ {}, "a한DEF글z", 4u, 7u });
    ASSERT_EQ(begun.admission, TextInputAdmission::Accepted);
    const u64 revision = service.surroundingRevision(begun.token);
    EXPECT_EQ(service.emitDeleteSurrounding(begun.token, 1u, 0u, revision, TextInputDeletionBasis::Selection),
        TextInputAdmission::InvalidRange);
    EXPECT_EQ(service.emitDeleteSurrounding(begun.token, 0u, 1u, revision, TextInputDeletionBasis::Selection),
        TextInputAdmission::InvalidRange);
    ASSERT_EQ(service.emitDeleteSurrounding(begun.token, 3u, 3u, revision, TextInputDeletionBasis::Selection),
        TextInputAdmission::Accepted);
    TextInputEvent event(arena.arena);
    ASSERT_EQ(service.poll(begun.token, event), TextInputPollResult::Event);
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
        ASSERT_EQ(begun.admission, TextInputAdmission::Accepted);
        const u64 revision = service.surroundingRevision(begun.token);
        token = begun.token;
        ASSERT_EQ(service.emitDeleteSurrounding(token, 2u, 1u, revision, TextInputDeletionBasis::Selection),
            TextInputAdmission::Accepted);
        ASSERT_EQ(service.poll(token, event), TextInputPollResult::Event);
        EXPECT_EQ(event.deletionBasis, TextInputDeletionBasis::Selection);
        ASSERT_EQ(service.emitCommit(token, "X"), TextInputAdmission::Accepted);
        ASSERT_EQ(service.poll(token, event), TextInputPollResult::Event);
        EXPECT_EQ(event.deletionBasis, TextInputDeletionBasis::Caret);
        ASSERT_EQ(service.emitDeleteSurrounding(token, 0u, 0u, revision, TextInputDeletionBasis::Selection),
            TextInputAdmission::Accepted);
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
    ASSERT_EQ(begun.admission, TextInputAdmission::Accepted);
    const u64 revision = service.surroundingRevision(begun.token);
    ASSERT_EQ(service.emitPreedit(begun.token, {}, 0u, 0u), TextInputAdmission::Accepted);
    ASSERT_EQ(service.emitDeleteSurrounding(begun.token, 2u, 1u, revision, TextInputDeletionBasis::Selection),
        TextInputAdmission::Accepted);
    ASSERT_EQ(service.emitCommit(begun.token, "X"), TextInputAdmission::Accepted);
    ASSERT_EQ(service.emitPreedit(begun.token, "next", 0u, 4u), TextInputAdmission::Accepted);
    const Array<TextInputEventKind::Enum, 4u> kinds{
        TextInputEventKind::Preedit, TextInputEventKind::DeleteSurrounding,
        TextInputEventKind::Commit, TextInputEventKind::Preedit
    };
    TextInputEvent event(arena.arena);
    u64 previousSequence = 0u;
    for(const auto kind : kinds){
        ASSERT_EQ(service.poll(begun.token, event), TextInputPollResult::Event);
        EXPECT_EQ(event.kind, kind);
        EXPECT_GT(event.sequence, previousSequence);
        EXPECT_EQ(event.deletionBasis, kind == TextInputEventKind::DeleteSurrounding
            ? TextInputDeletionBasis::Selection : TextInputDeletionBasis::Caret);
        previousSequence = event.sequence;
    }
    EXPECT_EQ(service.poll(begun.token, event), TextInputPollResult::Pending);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

