// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/text_edit_session.h>

#include <core/os/text_input_service.h>
#include <tests/common/test_context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_text_edit_owned_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Core;
using namespace NWB::Impl;

class OwnedTextInput final : public QueuedTextInputService{
public:
    explicit OwnedTextInput(Alloc::GlobalArena& arena)
        : QueuedTextInputService(arena)
    {}


public:
    [[nodiscard]] virtual TextInputCapabilities capabilities()const noexcept override{ return { true, true, true, true }; }
    [[nodiscard]] TextInputAdmission::Enum commit(const AStringView text){ return emitCommit(activeSession(), text); }
    [[nodiscard]] TextInputAdmission::Enum preedit(const AStringView text, const usize anchor, const usize caret){
        return emitPreedit(activeSession(), text, anchor, caret, false);
    }


protected:
    virtual void updateNativeSurrounding(
        AStringView text, usize anchor, usize caret, u64 revision, const TextInputChangeCause::Enum cause)override{
        static_cast<void>(text);
        static_cast<void>(anchor);
        static_cast<void>(caret);
        static_cast<void>(revision);
        lastCause = cause;
    }


public:
    TextInputChangeCause::Enum lastCause = TextInputChangeCause::Other;
};

inline constexpr UiTextEditOwner s_Owner{ { 61u }, 1u, 2u };
inline constexpr TextInputRect s_Caret{ 10, 20, 1, 18 };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(UiTextEditOwned, PollCopiesEventsWithoutApplyingAndOwnedCommitSurvivesNativeCancellation){
    Tests::TestArena arena;
    OwnedTextInput service(arena.arena);
    Ui::EditModel model(arena.arena);
    UiTextEditSession session(arena.arena, service);
    TextInputEvent event(arena.arena);
    ASSERT_TRUE(service.setFocused(true));
    ASSERT_TRUE(model.setText("base"));
    ASSERT_EQ(session.begin(s_Owner, model, s_Caret), TextInputAdmission::Accepted);
    const auto oldToken = session.token();
    AString<Alloc::GlobalArena> source(" copied", arena.arena);
    ASSERT_EQ(service.commit(source), TextInputAdmission::Accepted);
    source.assign("changed");
    ASSERT_EQ(session.pollOwned(event), TextInputPollResult::Event);
    EXPECT_EQ(event.token, oldToken);
    EXPECT_EQ(event.text, " copied");
    EXPECT_EQ(model.text(), "base");
    EXPECT_TRUE(session.matchesPublished(s_Owner, model));
    ASSERT_TRUE(session.cancel());
    EXPECT_FALSE(session.token().valid());
    EXPECT_FALSE(service.activeSession().valid());
    EXPECT_EQ(session.pollOwned(event), TextInputPollResult::InvalidSession);
    ASSERT_EQ(ApplyUiTextEditEvent(model, event, 0u, false), UiTextEditStatus::Applied);
    EXPECT_EQ(model.text(), "base copied");
}

TEST(UiTextEditOwned, TrustedLocalEditPublishesOtherCauseWhileOrdinaryRefreshRemainsStrict){
    Tests::TestArena arena;
    OwnedTextInput service(arena.arena);
    Ui::EditModel model(arena.arena);
    UiTextEditSession session(arena.arena, service);
    ASSERT_TRUE(service.setFocused(true));
    ASSERT_TRUE(model.setText("base"));
    ASSERT_EQ(session.begin(s_Owner, model, s_Caret), TextInputAdmission::Accepted);
    const u64 oldSurrounding = session.surroundingRevision();
    ASSERT_TRUE(model.replaceSelection(" local"));
    EXPECT_FALSE(session.matchesPublished(s_Owner, model));
    EXPECT_EQ(session.refresh(s_Owner, model, s_Caret), TextInputAdmission::InvalidSession);
    ASSERT_EQ(session.adoptLocal(s_Owner, model, s_Caret), TextInputAdmission::Accepted);
    EXPECT_GT(session.surroundingRevision(), oldSurrounding);
    EXPECT_EQ(service.lastCause, TextInputChangeCause::Other);
    EXPECT_TRUE(session.matchesPublished(s_Owner, model));
    ASSERT_EQ(service.commit(" native"), TextInputAdmission::Accepted);
    EXPECT_TRUE(session.drain(s_Owner, model).textChanged);
    EXPECT_EQ(model.text(), "base local native");
}

TEST(UiTextEditOwned, OrderedPreeditAndCommitCanBeAdoptedWithInputMethodCause){
    Tests::TestArena arena;
    OwnedTextInput service(arena.arena);
    Ui::EditModel model(arena.arena);
    UiTextEditSession session(arena.arena, service);
    TextInputEvent event(arena.arena);
    ASSERT_TRUE(service.setFocused(true));
    ASSERT_TRUE(model.setText("abcd"));
    ASSERT_TRUE(model.setSelection(1u, 3u));
    ASSERT_EQ(session.begin(s_Owner, model, s_Caret), TextInputAdmission::Accepted);
    ASSERT_EQ(service.preedit("draft", 0u, 5u), TextInputAdmission::Accepted);
    ASSERT_EQ(service.commit("Z"), TextInputAdmission::Accepted);
    ASSERT_EQ(session.pollOwned(event), TextInputPollResult::Event);
    EXPECT_FALSE(event.caretVisible);
    ASSERT_EQ(ApplyUiTextEditEvent(model, event, session.surroundingRevision(), session.matchesPublished(s_Owner, model)),
        UiTextEditStatus::Applied);
    ASSERT_EQ(session.adoptLocal(s_Owner, model, s_Caret, TextInputChangeCause::InputMethod), TextInputAdmission::Accepted);
    EXPECT_EQ(model.composition().text, "draft");
    ASSERT_EQ(session.pollOwned(event), TextInputPollResult::Event);
    ASSERT_EQ(ApplyUiTextEditEvent(model, event, session.surroundingRevision(), session.matchesPublished(s_Owner, model)),
        UiTextEditStatus::Applied);
    ASSERT_EQ(session.adoptLocal(s_Owner, model, s_Caret, TextInputChangeCause::InputMethod), TextInputAdmission::Accepted);
    EXPECT_EQ(model.text(), "aZd");
    EXPECT_EQ(service.lastCause, TextInputChangeCause::InputMethod);
    EXPECT_EQ(session.refresh(s_Owner, model, s_Caret), TextInputAdmission::Accepted);
    EXPECT_EQ(session.drain(s_Owner, model).status, UiTextEditStatus::Idle);
}

TEST(UiTextEditOwned, CancelWithoutModelLeavesTransientCompositionForTheNextSynchronousLend){
    Tests::TestArena arena;
    OwnedTextInput service(arena.arena);
    Ui::EditModel model(arena.arena);
    UiTextEditSession session(arena.arena, service);
    ASSERT_TRUE(service.setFocused(true));
    ASSERT_TRUE(model.setText("base"));
    ASSERT_EQ(session.begin(s_Owner, model, s_Caret), TextInputAdmission::Accepted);
    ASSERT_EQ(service.preedit("draft", 0u, 5u), TextInputAdmission::Accepted);
    ASSERT_EQ(session.drain(s_Owner, model).status, UiTextEditStatus::Applied);
    ASSERT_TRUE(session.cancel());
    EXPECT_TRUE(model.composition().active);
    EXPECT_EQ(model.text(), "base");
    EXPECT_EQ(session.adoptLocal(s_Owner, model, s_Caret), TextInputAdmission::InvalidSession);
    model.cancelComposition();
    ASSERT_EQ(session.begin(s_Owner, model, s_Caret), TextInputAdmission::Accepted);
}

TEST(UiTextEditOwned, CopiedSurroundingDeletionRequiresPublishedRevisionAndGraphemeAlignedRange){
    Tests::TestArena arena;
    Ui::EditModel model(arena.arena);
    TextInputEvent event(arena.arena);
    ASSERT_TRUE(model.setText("a\xCC\x81z"));
    event.kind = TextInputEventKind::DeleteSurrounding;
    event.surroundingRevision = 7u;
    event.deleteBeforeBytes = 1u;
    EXPECT_EQ(ApplyUiTextEditEvent(model, event, 8u, true), UiTextEditStatus::StaleSurrounding);
    EXPECT_EQ(ApplyUiTextEditEvent(model, event, 7u, false), UiTextEditStatus::StaleSurrounding);
    EXPECT_EQ(model.text(), "a\xCC\x81z");
    event.deleteBeforeBytes = 2u;
    EXPECT_EQ(ApplyUiTextEditEvent(model, event, 7u, true), UiTextEditStatus::ModelRejected);
    EXPECT_EQ(model.text(), "a\xCC\x81z");
    event.deleteBeforeBytes = 5u;
    EXPECT_EQ(ApplyUiTextEditEvent(model, event, 7u, true), UiTextEditStatus::InvalidEvent);
    event.deleteBeforeBytes = 1u;
    EXPECT_EQ(ApplyUiTextEditEvent(model, event, 7u, true), UiTextEditStatus::Applied);
    EXPECT_EQ(model.text(), "a\xCC\x81");
}

TEST(UiTextEditOwned, MalformedCopiedEventsAndRejectedPreeditPreserveCommittedState){
    Tests::TestArena arena;
    Ui::EditModel model(arena.arena, { 4u });
    TextInputEvent event(arena.arena);
    ASSERT_TRUE(model.setText("base"));
    event.kind = TextInputEventKind::Preedit;
    event.text.assign("\xED\x95\x9C");
    event.anchorByte = 1u;
    event.caretByte = 3u;
    EXPECT_EQ(ApplyUiTextEditEvent(model, event, 0u, false), UiTextEditStatus::InvalidEvent);
    EXPECT_FALSE(model.composition().active);
    event.anchorByte = 0u;
    EXPECT_EQ(ApplyUiTextEditEvent(model, event, 0u, false), UiTextEditStatus::ModelRejected);
    EXPECT_FALSE(model.composition().active);
    event.kind = TextInputEventKind::Commit;
    event.text.assign("\xC0\xAF");
    EXPECT_EQ(ApplyUiTextEditEvent(model, event, 0u, false), UiTextEditStatus::InvalidEvent);
    EXPECT_EQ(model.text(), "base");
    ASSERT_TRUE(model.beginComposition());
    event.kind = TextInputEventKind::Cancelled;
    EXPECT_EQ(ApplyUiTextEditEvent(model, event, 0u, false), UiTextEditStatus::Cancelled);
    EXPECT_FALSE(model.composition().active);
}

TEST(UiTextEditOwned, IdenticalExternalResetFencesOldEventsAndPublishedDeletionContext){
    Tests::TestArena arena;
    OwnedTextInput service(arena.arena);
    Ui::EditModel model(arena.arena);
    UiTextEditSession session(arena.arena, service);
    ASSERT_TRUE(service.setFocused(true));
    ASSERT_TRUE(model.setText("base"));
    ASSERT_EQ(session.begin(s_Owner, model, s_Caret), TextInputAdmission::Accepted);
    ASSERT_EQ(service.commit("old"), TextInputAdmission::Accepted);
    const u64 revision = model.revision();
    ASSERT_TRUE(model.setText("base"));
    EXPECT_EQ(model.revision(), revision);
    EXPECT_FALSE(session.matchesPublished(s_Owner, model));
    EXPECT_EQ(session.refresh(s_Owner, model, s_Caret), TextInputAdmission::InvalidSession);
    EXPECT_EQ(session.drain(s_Owner, model).status, UiTextEditStatus::StaleModel);
    EXPECT_EQ(model.text(), "base");
}

TEST(UiTextEditOwned, OwnedCollectionAdoptionAndCancellationRejectAnotherThread){
    Tests::TestArena arena;
    OwnedTextInput service(arena.arena);
    Ui::EditModel model(arena.arena);
    UiTextEditSession session(arena.arena, service);
    TextInputEvent event(arena.arena);
    ASSERT_TRUE(service.setFocused(true));
    ASSERT_EQ(session.begin(s_Owner, model, s_Caret), TextInputAdmission::Accepted);
    TextInputPollResult::Enum poll = TextInputPollResult::Pending;
    TextInputAdmission::Enum adopt = TextInputAdmission::Accepted;
    bool cancelled = true;
    Thread other([&](){
        poll = session.pollOwned(event);
        adopt = session.adoptLocal(s_Owner, model, s_Caret);
        cancelled = session.cancel();
    });
    other.join();
    EXPECT_EQ(poll, TextInputPollResult::WrongThread);
    EXPECT_EQ(adopt, TextInputAdmission::WrongThread);
    EXPECT_FALSE(cancelled);
    EXPECT_TRUE(session.token().valid());
    EXPECT_TRUE(session.cancel());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

