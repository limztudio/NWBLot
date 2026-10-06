// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/text_edit_session.h>
#include <core/os/text_input_service.h>
#include <tests/common/test_context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_text_edit_session_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core;
using namespace NWB::Impl;

class FakeTextInput final : public QueuedTextInputService{
public:
    explicit FakeTextInput(Alloc::GlobalArena& arena)
        : QueuedTextInputService(arena)
    {}


public:
    [[nodiscard]] virtual TextInputCapabilities capabilities()const noexcept override{ return { true, true, true, true }; }
    [[nodiscard]] TextInputAdmission::Enum commit(AStringView text){ return emitCommit(activeSession(), text); }
    [[nodiscard]] TextInputAdmission::Enum preedit(AStringView text, usize anchor, usize caret, bool visible = true){
        return emitPreedit(activeSession(), text, anchor, caret, visible);
    }
    [[nodiscard]] TextInputAdmission::Enum erase(usize before, usize after, u64 revision, TextInputDeletionBasis::Enum basis){
        return emitDeleteSurrounding(activeSession(), before, after, revision, basis);
    }
};

inline constexpr UiTextEditOwner s_Owner{ { 17u }, 1u, 1u };
inline constexpr TextInputRect s_Caret{ 100, 32, 1, 18 };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(UiTextEditSession, TrailingPreeditClearPreservesCommittedSelectedReplacementAndSingleUndo){
    NWB::Tests::TestArena arena;
    FakeTextInput service(arena.arena);
    Ui::EditModel model(arena.arena);
    UiTextEditSession session(arena.arena, service);
    ASSERT_TRUE(service.setFocused(true));
    ASSERT_TRUE(model.setText("abcd"));
    ASSERT_TRUE(model.setSelection(1u, 3u));
    ASSERT_EQ(session.begin(s_Owner, model, s_Caret), TextInputAdmission::Accepted);
    ASSERT_EQ(service.preedit("xy", 0u, 2u), TextInputAdmission::Accepted);
    EXPECT_EQ(session.drain(s_Owner, model).status, UiTextEditStatus::Applied);
    EXPECT_EQ(model.text(), "abcd");
    EXPECT_FALSE(model.canUndo());
    ASSERT_EQ(service.preedit("xyz", 1u, 3u), TextInputAdmission::Accepted);
    ASSERT_EQ(service.commit("Z"), TextInputAdmission::Accepted);
    ASSERT_EQ(service.preedit({}, 0u, 0u), TextInputAdmission::Accepted);
    const auto result = session.drain(s_Owner, model);
    EXPECT_EQ(result.eventsApplied, 3u);
    EXPECT_EQ(model.text(), "aZd");
    EXPECT_FALSE(model.composition().active);
    ASSERT_TRUE(session.end(s_Owner, model));
    ASSERT_TRUE(model.undo());
    EXPECT_EQ(model.text(), "abcd");
    EXPECT_EQ(model.anchor(), 1u);
    EXPECT_EQ(model.caret(), 3u);
    EXPECT_FALSE(model.canUndo());
}

TEST(UiTextEditSession, EmptyPreeditCancelsWithoutChangingCommittedText){
    NWB::Tests::TestArena arena;
    FakeTextInput service(arena.arena);
    Ui::EditModel model(arena.arena);
    UiTextEditSession session(arena.arena, service);
    ASSERT_TRUE(service.setFocused(true));
    ASSERT_TRUE(model.setText("base"));
    ASSERT_EQ(session.begin(s_Owner, model, s_Caret), TextInputAdmission::Accepted);
    ASSERT_EQ(service.preedit("transient", 0u, 9u), TextInputAdmission::Accepted);
    ASSERT_EQ(service.preedit({}, 0u, 0u), TextInputAdmission::Accepted);
    EXPECT_EQ(session.drain(s_Owner, model).status, UiTextEditStatus::Applied);
    EXPECT_EQ(model.text(), "base");
    EXPECT_FALSE(model.composition().active);
    EXPECT_FALSE(model.canUndo());
}

TEST(UiTextEditSession, FocusLossDiscardsQueuedCommitAndClearsOnlyTransientPreedit){
    NWB::Tests::TestArena arena;
    FakeTextInput service(arena.arena);
    Ui::EditModel model(arena.arena);
    UiTextEditSession session(arena.arena, service);
    ASSERT_TRUE(service.setFocused(true));
    ASSERT_TRUE(model.setText("base"));
    ASSERT_EQ(session.begin(s_Owner, model, s_Caret), TextInputAdmission::Accepted);
    ASSERT_EQ(service.preedit("new", 0u, 3u), TextInputAdmission::Accepted);
    EXPECT_EQ(session.drain(s_Owner, model).status, UiTextEditStatus::Applied);
    ASSERT_EQ(service.commit("lost"), TextInputAdmission::Accepted);
    ASSERT_TRUE(service.setFocused(false));
    EXPECT_EQ(session.drain(s_Owner, model).status, UiTextEditStatus::Cancelled);
    EXPECT_EQ(model.text(), "base");
    EXPECT_FALSE(model.composition().active);
    EXPECT_FALSE(session.token().valid());
}

TEST(UiTextEditSession, ReplacedOwnerCannotReceiveOldQueuedCommit){
    NWB::Tests::TestArena arena;
    FakeTextInput service(arena.arena);
    Ui::EditModel oldModel(arena.arena);
    Ui::EditModel newModel(arena.arena);
    UiTextEditSession session(arena.arena, service);
    ASSERT_TRUE(service.setFocused(true));
    ASSERT_TRUE(newModel.setText("replacement"));
    ASSERT_EQ(session.begin(s_Owner, oldModel, s_Caret), TextInputAdmission::Accepted);
    ASSERT_EQ(service.commit("old"), TextInputAdmission::Accepted);
    const UiTextEditOwner replacement{ s_Owner.widget, s_Owner.declarationGeneration, 2u };
    EXPECT_EQ(session.drain(replacement, newModel).status, UiTextEditStatus::StaleOwner);
    EXPECT_EQ(newModel.text(), "replacement");
    EXPECT_FALSE(service.activeSession().valid());
    EXPECT_FALSE(session.token().valid());
}

TEST(UiTextEditSession, ExternalTextAndSelectionChangesFenceOldNativeEvents){
    NWB::Tests::TestArena arena;
    FakeTextInput service(arena.arena);
    Ui::EditModel model(arena.arena);
    UiTextEditSession session(arena.arena, service);
    ASSERT_TRUE(service.setFocused(true));
    ASSERT_TRUE(model.setText("abcd"));
    ASSERT_EQ(session.begin(s_Owner, model, s_Caret), TextInputAdmission::Accepted);
    ASSERT_EQ(service.commit("late"), TextInputAdmission::Accepted);
    ASSERT_TRUE(model.setSelection(0u, 2u));
    EXPECT_EQ(session.refresh(s_Owner, model, s_Caret), TextInputAdmission::InvalidSession);
    EXPECT_EQ(session.drain(s_Owner, model).status, UiTextEditStatus::StaleModel);
    EXPECT_EQ(model.text(), "abcd");
    EXPECT_EQ(model.anchor(), 0u);
    EXPECT_EQ(model.caret(), 2u);
    ASSERT_EQ(session.begin(s_Owner, model, s_Caret), TextInputAdmission::Accepted);
    ASSERT_EQ(service.commit("late"), TextInputAdmission::Accepted);
    ASSERT_TRUE(model.setText("host value"));
    EXPECT_EQ(session.drain(s_Owner, model).status, UiTextEditStatus::StaleModel);
    EXPECT_EQ(model.text(), "host value");
}

TEST(UiTextEditSession, DeleteAddressesPublishedUtf8BytesThenCommitUsesUpdatedSelection){
    NWB::Tests::TestArena arena;
    FakeTextInput service(arena.arena);
    Ui::EditModel model(arena.arena);
    UiTextEditSession session(arena.arena, service);
    ASSERT_TRUE(service.setFocused(true));
    ASSERT_TRUE(model.setText("abCde"));
    ASSERT_TRUE(model.setSelection(3u, 3u));
    ASSERT_EQ(session.begin(s_Owner, model, s_Caret), TextInputAdmission::Accepted);
    ASSERT_EQ(
        service.erase(1u, 1u, service.surroundingRevision(service.activeSession()), TextInputDeletionBasis::Caret),
        TextInputAdmission::Accepted
    );
    ASSERT_EQ(service.commit("X"), TextInputAdmission::Accepted);
    const auto result = session.drain(s_Owner, model);
    EXPECT_EQ(result.status, UiTextEditStatus::Applied);
    EXPECT_EQ(result.eventsApplied, 2u);
    EXPECT_EQ(model.text(), "abXe");
}

TEST(UiTextEditSession, SelectedSurroundingDeleteCannotMisapplyWaylandExclusionSemantics){
    NWB::Tests::TestArena arena;
    FakeTextInput service(arena.arena);
    Ui::EditModel model(arena.arena);
    UiTextEditSession session(arena.arena, service);
    ASSERT_TRUE(service.setFocused(true));
    ASSERT_TRUE(model.setText("abcDEFghi"));
    ASSERT_TRUE(model.setSelection(3u, 6u));
    ASSERT_EQ(session.begin(s_Owner, model, s_Caret), TextInputAdmission::Accepted);
    ASSERT_EQ(
        service.erase(1u, 1u, service.surroundingRevision(service.activeSession()), TextInputDeletionBasis::Caret),
        TextInputAdmission::Accepted
    );
    ASSERT_EQ(service.commit("X"), TextInputAdmission::Accepted);
    EXPECT_EQ(session.drain(s_Owner, model).status, UiTextEditStatus::ModelRejected);
    EXPECT_EQ(model.text(), "abcDEFghi");
    EXPECT_EQ(model.anchor(), 3u);
    EXPECT_EQ(model.caret(), 6u);
    EXPECT_FALSE(model.canUndo());
    EXPECT_FALSE(service.activeSession().valid());
}

TEST(UiTextEditSession, PreeditClearBeforeCommitPreservesTheOriginalReplacementSelection){
    NWB::Tests::TestArena arena;
    FakeTextInput service(arena.arena);
    Ui::EditModel model(arena.arena);
    UiTextEditSession session(arena.arena, service);
    ASSERT_TRUE(service.setFocused(true));
    ASSERT_TRUE(model.setText("abcd"));
    ASSERT_TRUE(model.setSelection(3u, 1u));
    ASSERT_EQ(session.begin(s_Owner, model, s_Caret), TextInputAdmission::Accepted);
    ASSERT_EQ(service.preedit("pending", 0u, 7u), TextInputAdmission::Accepted);
    EXPECT_EQ(session.drain(s_Owner, model).status, UiTextEditStatus::Applied);
    ASSERT_EQ(service.preedit({}, 0u, 0u), TextInputAdmission::Accepted);
    ASSERT_EQ(service.commit("X"), TextInputAdmission::Accepted);
    EXPECT_EQ(session.drain(s_Owner, model).status, UiTextEditStatus::Applied);
    EXPECT_EQ(model.text(), "aXd");
    ASSERT_TRUE(session.end(s_Owner, model));
    ASSERT_TRUE(model.undo());
    EXPECT_EQ(model.anchor(), 3u);
    EXPECT_EQ(model.caret(), 1u);
}

TEST(UiTextEditSession, DeleteAfterUnpublishedCommitCannotDeleteDifferentText){
    NWB::Tests::TestArena arena;
    FakeTextInput service(arena.arena);
    Ui::EditModel model(arena.arena);
    UiTextEditSession session(arena.arena, service);
    ASSERT_TRUE(service.setFocused(true));
    ASSERT_TRUE(model.setText("ab"));
    ASSERT_EQ(session.begin(s_Owner, model, s_Caret), TextInputAdmission::Accepted);
    ASSERT_EQ(service.commit("X"), TextInputAdmission::Accepted);
    ASSERT_EQ(
        service.erase(1u, 0u, service.surroundingRevision(service.activeSession()), TextInputDeletionBasis::Caret),
        TextInputAdmission::Accepted
    );
    EXPECT_EQ(session.drain(s_Owner, model).status, UiTextEditStatus::StaleSurrounding);
    EXPECT_EQ(model.text(), "abX");
    EXPECT_FALSE(session.token().valid());
}

TEST(UiTextEditSession, NativeByteDeleteCannotSplitAGrapheme){
    NWB::Tests::TestArena arena;
    FakeTextInput service(arena.arena);
    Ui::EditModel model(arena.arena);
    UiTextEditSession session(arena.arena, service);
    ASSERT_TRUE(service.setFocused(true));
    ASSERT_TRUE(model.setText("e\xCC\x81"));
    ASSERT_EQ(session.begin(s_Owner, model, s_Caret), TextInputAdmission::Accepted);
    ASSERT_EQ(
        service.erase(2u, 0u, service.surroundingRevision(service.activeSession()), TextInputDeletionBasis::Caret),
        TextInputAdmission::Accepted
    );
    EXPECT_EQ(session.drain(s_Owner, model).status, UiTextEditStatus::ModelRejected);
    EXPECT_EQ(model.text(), "e\xCC\x81");
    EXPECT_FALSE(model.canUndo());
}

TEST(UiTextEditSession, RejectedCommitPreservesCommittedTextAndCancelsNativeSession){
    NWB::Tests::TestArena arena;
    FakeTextInput service(arena.arena);
    Ui::EditModel model(arena.arena, { 2u, 4u, 128u });
    UiTextEditSession session(arena.arena, service);
    ASSERT_TRUE(service.setFocused(true));
    ASSERT_TRUE(model.setText("ab"));
    ASSERT_EQ(session.begin(s_Owner, model, s_Caret), TextInputAdmission::Accepted);
    ASSERT_EQ(service.commit("too large"), TextInputAdmission::Accepted);
    EXPECT_EQ(session.drain(s_Owner, model).status, UiTextEditStatus::ModelRejected);
    EXPECT_EQ(model.text(), "ab");
    EXPECT_FALSE(model.canUndo());
    EXPECT_FALSE(service.activeSession().valid());
}

TEST(UiTextEditSession, ExplicitEndClearsCompositionAndInvalidatesOldToken){
    NWB::Tests::TestArena arena;
    FakeTextInput service(arena.arena);
    Ui::EditModel model(arena.arena);
    UiTextEditSession session(arena.arena, service);
    ASSERT_TRUE(service.setFocused(true));
    ASSERT_EQ(session.begin(s_Owner, model, s_Caret), TextInputAdmission::Accepted);
    const auto first = session.token();
    EXPECT_EQ(session.begin(s_Owner, model, s_Caret), TextInputAdmission::Busy);
    ASSERT_EQ(service.preedit("pending", 0u, 7u), TextInputAdmission::Accepted);
    EXPECT_EQ(session.drain(s_Owner, model).status, UiTextEditStatus::Applied);
    ASSERT_TRUE(session.end(s_Owner, model));
    EXPECT_FALSE(model.composition().active);
    ASSERT_EQ(session.begin(s_Owner, model, s_Caret), TextInputAdmission::Accepted);
    EXPECT_NE(session.token(), first);
    TextInputEvent event(arena.arena);
    EXPECT_EQ(service.poll(first, event), TextInputPollResult::InvalidSession);
}

TEST(UiTextEditSession, DestructionReleasesServiceWithoutBorrowingTheDestroyedModel){
    NWB::Tests::TestArena arena;
    FakeTextInput service(arena.arena);
    ASSERT_TRUE(service.setFocused(true));
    {
        UiTextEditSession session(arena.arena, service);
        {
            Ui::EditModel model(arena.arena);
            ASSERT_EQ(session.begin(s_Owner, model, s_Caret), TextInputAdmission::Accepted);
        }
        EXPECT_TRUE(service.activeSession().valid());
    }
    EXPECT_FALSE(service.activeSession().valid());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

