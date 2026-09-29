// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_box_host_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_selection_deletion_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace UiEditBoxTestSupport;

namespace DeletionMutation{
    enum Enum : u8{ SelectionRoundTrip, TextUndoRoundTrip, ExternalReset };
};

class UiSelectionDeletionTests : public UiEditBoxHostTests{
protected:
    [[nodiscard]] UiTextEditOwner owner()const;
    [[nodiscard]] bool seed(bool reverse = false);
    [[nodiscard]] bool mutate(DeletionMutation::Enum mutation);
    void expectBaseline(u64 revision, u64 externalRevision, u64 selectionGeneration)const;
    void sessionOrder(bool reverse);
    void sessionRejected(DeletionMutation::Enum mutation);
    void hostRejected(DeletionMutation::Enum mutation);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiTextEditOwner UiSelectionDeletionTests::owner()const{
    return { { 371u }, 3u, m_model.instanceGeneration() };
}

bool UiSelectionDeletionTests::seed(const bool reverse){
    return m_model.setText("abcDEFghi") && m_model.setSelection(reverse ? 6u : 3u, reverse ? 3u : 6u);
}

bool UiSelectionDeletionTests::mutate(const DeletionMutation::Enum mutation){
    const usize anchor = m_model.anchor();
    const usize caret = m_model.caret();
    switch(mutation){
    case DeletionMutation::SelectionRoundTrip:
        return m_model.setSelection(0u, 0u) && m_model.setSelection(anchor, caret);
    case DeletionMutation::TextUndoRoundTrip:
        return m_model.replaceSelection("XYZ") && m_model.undo();
    case DeletionMutation::ExternalReset:
        return m_model.setText("abcDEFghi") && m_model.setSelection(anchor, caret);
    default:
        return false;
    }
}

void UiSelectionDeletionTests::expectBaseline(
    const u64 revision, const u64 externalRevision, const u64 selectionGeneration)const{
    EXPECT_EQ(m_model.text(), "abcDEFghi");
    EXPECT_EQ(m_model.anchor(), 3u);
    EXPECT_EQ(m_model.caret(), 6u);
    EXPECT_EQ(m_model.revision(), revision);
    EXPECT_EQ(m_model.externalRevision(), externalRevision);
    EXPECT_EQ(m_model.selectionGeneration(), selectionGeneration);
    EXPECT_FALSE(m_model.composition().active);
}

void UiSelectionDeletionTests::sessionOrder(const bool reverse){
    ASSERT_TRUE(seed(reverse));
    UiTextEditSession session(m_arena, m_textInput);
    ASSERT_EQ(session.begin(owner(), m_model, { 30, 40, 1, 18 }), TextInputAdmission::Accepted);
    ASSERT_EQ(m_textInput.preedit("IME", 0u, 3u), TextInputAdmission::Accepted);
    ASSERT_EQ(session.drain(owner(), m_model).status, UiTextEditStatus::Applied);
    ASSERT_TRUE(m_model.composition().active);
    EXPECT_FALSE(m_model.canUndo());
    const u64 surrounding = session.surroundingRevision();
    ASSERT_EQ(m_textInput.preedit({}, 0u, 0u), TextInputAdmission::Accepted);
    ASSERT_EQ(m_textInput.erase(1u, 1u, surrounding, TextInputDeletionBasis::Selection), TextInputAdmission::Accepted);
    ASSERT_EQ(m_textInput.commit("X"), TextInputAdmission::Accepted);
    const UiTextEditResult result = session.drain(owner(), m_model);
    EXPECT_EQ(result.status, UiTextEditStatus::Applied);
    EXPECT_EQ(result.eventsApplied, 3u);
    EXPECT_TRUE(result.textChanged);
    EXPECT_EQ(m_model.text(), "abXhi");
    EXPECT_EQ(m_model.anchor(), 3u);
    EXPECT_EQ(m_model.caret(), 3u);
    EXPECT_FALSE(m_model.composition().active);
    ASSERT_TRUE(session.end(owner(), m_model));
    ASSERT_TRUE(m_model.undo());
    EXPECT_EQ(m_model.text(), "abDEFhi");
    EXPECT_EQ(m_model.anchor(), reverse ? 5u : 2u);
    EXPECT_EQ(m_model.caret(), reverse ? 2u : 5u);
    ASSERT_TRUE(m_model.undo());
    EXPECT_EQ(m_model.text(), "abcDEFghi");
    EXPECT_EQ(m_model.anchor(), reverse ? 6u : 3u);
    EXPECT_EQ(m_model.caret(), reverse ? 3u : 6u);
    EXPECT_FALSE(m_model.canUndo());
}

void UiSelectionDeletionTests::sessionRejected(const DeletionMutation::Enum mutation){
    ASSERT_TRUE(seed());
    UiTextEditSession session(m_arena, m_textInput);
    ASSERT_EQ(session.begin(owner(), m_model, { 30, 40, 1, 18 }), TextInputAdmission::Accepted);
    const TextInputSessionToken token = session.token();
    ASSERT_EQ(m_textInput.erase(1u, 1u, 0u, TextInputDeletionBasis::Selection), TextInputAdmission::Accepted);
    ASSERT_EQ(m_textInput.commit("late"), TextInputAdmission::Accepted);
    ASSERT_TRUE(mutate(mutation));
    const u64 revision = m_model.revision();
    const u64 externalRevision = m_model.externalRevision();
    const u64 selectionGeneration = m_model.selectionGeneration();
    const u64 compositionGeneration = m_model.compositionGeneration();
    const bool canUndo = m_model.canUndo();
    const bool canRedo = m_model.canRedo();
    EXPECT_FALSE(session.matchesPublished(owner(), m_model));
    const UiTextEditResult result = session.drain(owner(), m_model);
    EXPECT_EQ(result.status, UiTextEditStatus::StaleModel);
    EXPECT_EQ(result.eventsApplied, 0u);
    EXPECT_FALSE(result.textChanged);
    expectBaseline(revision, externalRevision, selectionGeneration);
    EXPECT_EQ(m_model.compositionGeneration(), compositionGeneration);
    EXPECT_EQ(m_model.canUndo(), canUndo);
    EXPECT_EQ(m_model.canRedo(), canRedo);
    EXPECT_FALSE(session.token().valid());
    EXPECT_FALSE(m_textInput.activeSession().valid());
    EXPECT_EQ(m_textInput.commitFor(token, "later"), TextInputAdmission::InvalidSession);
}

void UiSelectionDeletionTests::hostRejected(const DeletionMutation::Enum mutation){
    ASSERT_TRUE(seed());
    ASSERT_TRUE(activate());
    const TextInputSessionToken token = m_textInput.activeSession();
    ASSERT_EQ(m_textInput.erase(1u, 1u, 0u, TextInputDeletionBasis::Selection), TextInputAdmission::Accepted);
    ASSERT_EQ(m_textInput.commit("late"), TextInputAdmission::Accepted);
    m_host.collectNative();
    ASSERT_TRUE(mutate(mutation));
    const u64 revision = m_model.revision();
    const u64 externalRevision = m_model.externalRevision();
    const u64 selectionGeneration = m_model.selectionGeneration();
    const bool canUndo = m_model.canUndo();
    const bool canRedo = m_model.canRedo();
    ASSERT_TRUE(frame(m_model));
    expectBaseline(revision, externalRevision, selectionGeneration);
    EXPECT_FALSE(m_result.textChanged);
    EXPECT_FALSE(m_result.selectionChanged);
    EXPECT_EQ(m_model.canUndo(), canUndo);
    EXPECT_EQ(m_model.canRedo(), canRedo);
    EXPECT_NE(m_textInput.activeSession(), token);
    EXPECT_EQ(m_textInput.commitFor(token, "later"), TextInputAdmission::InvalidSession);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiSelectionDeletionTests, ForwardSelectionClearDeleteCommitUsesTheAdjustedReplacementRange){
    sessionOrder(false);
}

TEST_F(UiSelectionDeletionTests, ReverseSelectionClearDeleteCommitRestoresDirectionThroughUndo){
    sessionOrder(true);
}

TEST_F(UiSelectionDeletionTests, CopiedSelectionBasisSurvivesNativeReleaseAndRetainsSelectedBytes){
    ASSERT_TRUE(seed());
    UiTextEditSession session(m_arena, m_textInput);
    ASSERT_EQ(session.begin(owner(), m_model, { 30, 40, 1, 18 }), TextInputAdmission::Accepted);
    const u64 surrounding = session.surroundingRevision();
    TextInputDeletionBasis::Enum basis = TextInputDeletionBasis::Selection;
    ASSERT_EQ(m_textInput.erase(1u, 1u, surrounding, basis), TextInputAdmission::Accepted);
    basis = TextInputDeletionBasis::Caret;
    TextInputEvent event(m_arena);
    EXPECT_EQ(event.deletionBasis, TextInputDeletionBasis::Caret);
    ASSERT_EQ(session.pollOwned(event), TextInputPollResult::Event);
    EXPECT_EQ(basis, TextInputDeletionBasis::Caret);
    EXPECT_EQ(event.kind, TextInputEventKind::DeleteSurrounding);
    EXPECT_EQ(event.deletionBasis, TextInputDeletionBasis::Selection);
    EXPECT_EQ(event.deleteBeforeBytes, 1u);
    EXPECT_EQ(event.deleteAfterBytes, 1u);
    EXPECT_EQ(event.surroundingRevision, surrounding);
    EXPECT_EQ(m_model.text(), "abcDEFghi");
    ASSERT_TRUE(session.cancel());
    ASSERT_EQ(ApplyUiTextEditEvent(m_model, event, surrounding, true), UiTextEditStatus::Applied);
    EXPECT_EQ(m_model.text(), "abDEFhi");
    EXPECT_EQ(m_model.selectedText(), "DEF");
    EXPECT_EQ(m_model.anchor(), 2u);
    EXPECT_EQ(m_model.caret(), 5u);
    ASSERT_TRUE(m_model.undo());
    EXPECT_EQ(m_model.text(), "abcDEFghi");
    EXPECT_EQ(m_model.anchor(), 3u);
    EXPECT_EQ(m_model.caret(), 6u);
    EXPECT_FALSE(m_model.canUndo());
}

TEST_F(UiSelectionDeletionTests, MalformedCopiedDeletionBasisRejectsWithoutChangingModelEpochs){
    ASSERT_TRUE(seed());
    const u64 revision = m_model.revision();
    const u64 externalRevision = m_model.externalRevision();
    const u64 selectionGeneration = m_model.selectionGeneration();
    const u64 compositionGeneration = m_model.compositionGeneration();
    TextInputEvent event(m_arena);
    event.kind = TextInputEventKind::DeleteSurrounding;
    event.surroundingRevision = 7u;
    event.deletionBasis = TextInputDeletionBasis::kCount;
    event.deleteBeforeBytes = 1u;
    event.deleteAfterBytes = 1u;
    EXPECT_EQ(ApplyUiTextEditEvent(m_model, event, 7u, true), UiTextEditStatus::InvalidEvent);
    expectBaseline(revision, externalRevision, selectionGeneration);
    EXPECT_EQ(m_model.compositionGeneration(), compositionGeneration);
    EXPECT_FALSE(m_model.canUndo());
    EXPECT_FALSE(m_model.canRedo());
}

TEST_F(UiSelectionDeletionTests, CaretDeletionStillRejectsANonemptySelectionAndDropsTheFollowingCommit){
    ASSERT_TRUE(seed());
    UiTextEditSession session(m_arena, m_textInput);
    ASSERT_EQ(session.begin(owner(), m_model, { 30, 40, 1, 18 }), TextInputAdmission::Accepted);
    ASSERT_EQ(m_textInput.erase(1u, 1u), TextInputAdmission::Accepted);
    ASSERT_EQ(m_textInput.commit("X"), TextInputAdmission::Accepted);
    const UiTextEditResult result = session.drain(owner(), m_model);
    EXPECT_EQ(result.status, UiTextEditStatus::ModelRejected);
    EXPECT_EQ(result.eventsApplied, 0u);
    EXPECT_FALSE(result.textChanged);
    EXPECT_EQ(m_model.text(), "abcDEFghi");
    EXPECT_EQ(m_model.anchor(), 3u);
    EXPECT_EQ(m_model.caret(), 6u);
    EXPECT_FALSE(m_model.canUndo());
    EXPECT_FALSE(session.token().valid());
    EXPECT_FALSE(m_textInput.activeSession().valid());
}

TEST_F(UiSelectionDeletionTests, ANewNativePublicationCannotReviveTheSessionsOldSurroundingSnapshot){
    ASSERT_TRUE(seed());
    UiTextEditSession session(m_arena, m_textInput);
    ASSERT_EQ(session.begin(owner(), m_model, { 30, 40, 1, 18 }), TextInputAdmission::Accepted);
    const u64 surrounding = session.surroundingRevision();
    const u64 revision = m_model.revision();
    const u64 externalRevision = m_model.externalRevision();
    const u64 selectionGeneration = m_model.selectionGeneration();
    ASSERT_EQ(m_textInput.updateSurrounding(session.token(), m_model.text(), 3u, 6u), TextInputAdmission::Accepted);
    ASSERT_GT(m_textInput.surroundingRevision(session.token()), surrounding);
    ASSERT_EQ(m_textInput.erase(1u, 1u, 0u, TextInputDeletionBasis::Selection), TextInputAdmission::Accepted);
    ASSERT_EQ(m_textInput.commit("late"), TextInputAdmission::Accepted);
    const UiTextEditResult result = session.drain(owner(), m_model);
    EXPECT_EQ(result.status, UiTextEditStatus::StaleSurrounding);
    EXPECT_EQ(result.eventsApplied, 0u);
    EXPECT_FALSE(result.textChanged);
    expectBaseline(revision, externalRevision, selectionGeneration);
    EXPECT_FALSE(m_model.canUndo());
    EXPECT_FALSE(session.token().valid());
    EXPECT_FALSE(m_textInput.activeSession().valid());
}

TEST_F(UiSelectionDeletionTests, AnEarlierUnpublishedCommitMakesTheQueuedSelectionDeletionStale){
    ASSERT_TRUE(seed());
    UiTextEditSession session(m_arena, m_textInput);
    ASSERT_EQ(session.begin(owner(), m_model, { 30, 40, 1, 18 }), TextInputAdmission::Accepted);
    ASSERT_EQ(m_textInput.commit("X"), TextInputAdmission::Accepted);
    ASSERT_EQ(m_textInput.erase(1u, 1u, 0u, TextInputDeletionBasis::Selection), TextInputAdmission::Accepted);
    ASSERT_EQ(m_textInput.commit("late"), TextInputAdmission::Accepted);
    const UiTextEditResult result = session.drain(owner(), m_model);
    EXPECT_EQ(result.status, UiTextEditStatus::StaleSurrounding);
    EXPECT_EQ(result.eventsApplied, 1u);
    EXPECT_TRUE(result.textChanged);
    EXPECT_EQ(m_model.text(), "abcXghi");
    EXPECT_FALSE(session.token().valid());
    EXPECT_FALSE(m_textInput.activeSession().valid());
    ASSERT_TRUE(m_model.undo());
    EXPECT_EQ(m_model.text(), "abcDEFghi");
    EXPECT_EQ(m_model.anchor(), 3u);
    EXPECT_EQ(m_model.caret(), 6u);
    EXPECT_FALSE(m_model.canUndo());
}

TEST_F(UiSelectionDeletionTests, QueuedSelectionDeletionRejectsSelectionAwayAndBack){
    sessionRejected(DeletionMutation::SelectionRoundTrip);
}

TEST_F(UiSelectionDeletionTests, QueuedSelectionDeletionRejectsLocalTextAndUndoRoundTrip){
    sessionRejected(DeletionMutation::TextUndoRoundTrip);
}

TEST_F(UiSelectionDeletionTests, QueuedSelectionDeletionRejectsIdenticalExternalTextReset){
    sessionRejected(DeletionMutation::ExternalReset);
}

TEST_F(UiSelectionDeletionTests, OrderedHostCopiesSelectionBasisAndPreservesBothDirectionalUndoSelections){
    for(const bool reverse : { false, true }){
        SCOPED_TRACE(reverse);
        ASSERT_TRUE(seed(reverse));
        ASSERT_TRUE(activate());
        ASSERT_EQ(m_textInput.preedit("IME", 0u, 3u), TextInputAdmission::Accepted);
        m_host.collectNative();
        ASSERT_TRUE(frame(m_model));
        ASSERT_TRUE(m_model.composition().active);
        ASSERT_EQ(m_textInput.preedit({}, 0u, 0u), TextInputAdmission::Accepted);
        ASSERT_EQ(m_textInput.erase(1u, 1u, 0u, TextInputDeletionBasis::Selection), TextInputAdmission::Accepted);
        ASSERT_EQ(m_textInput.commit("X"), TextInputAdmission::Accepted);
        m_host.collectNative();
        EXPECT_EQ(m_model.text(), "abcDEFghi");
        ASSERT_TRUE(frame(m_model));
        EXPECT_TRUE(m_result.textChanged);
        EXPECT_TRUE(m_result.selectionChanged);
        EXPECT_EQ(m_model.text(), "abXhi");
        EXPECT_FALSE(m_model.composition().active);
        ASSERT_TRUE(m_model.undo());
        EXPECT_EQ(m_model.text(), "abDEFhi");
        EXPECT_EQ(m_model.anchor(), reverse ? 5u : 2u);
        EXPECT_EQ(m_model.caret(), reverse ? 2u : 5u);
        ASSERT_TRUE(m_model.undo());
        EXPECT_EQ(m_model.text(), "abcDEFghi");
        EXPECT_EQ(m_model.anchor(), reverse ? 6u : 3u);
        EXPECT_EQ(m_model.caret(), reverse ? 3u : 6u);
        EXPECT_FALSE(m_model.canUndo());
    }
}

TEST_F(UiSelectionDeletionTests, ReadOnlyPolicyRetiresCollectedSelectionDeletionWithoutChangingCommittedText){
    ASSERT_TRUE(seed());
    ASSERT_TRUE(activate());
    const TextInputSessionToken token = m_textInput.activeSession();
    const u64 revision = m_model.revision();
    const u64 externalRevision = m_model.externalRevision();
    const u64 selectionGeneration = m_model.selectionGeneration();
    ASSERT_EQ(m_textInput.erase(1u, 1u, 0u, TextInputDeletionBasis::Selection), TextInputAdmission::Accepted);
    ASSERT_EQ(m_textInput.commit("X"), TextInputAdmission::Accepted);
    m_host.collectNative();
    Ui::EditBoxOptions options;
    options.readOnly = true;
    ASSERT_TRUE(frame(m_model, options));
    expectBaseline(revision, externalRevision, selectionGeneration);
    EXPECT_FALSE(m_result.textChanged);
    EXPECT_FALSE(m_result.selectionChanged);
    EXPECT_FALSE(m_model.canUndo());
    EXPECT_FALSE(m_textInput.activeSession().valid());
    EXPECT_EQ(m_textInput.commitFor(token, "later"), TextInputAdmission::InvalidSession);
}

TEST_F(UiSelectionDeletionTests, OrderedHostFencesApplicationTextSelectionAndExternalOwnershipEpochs){
    const DeletionMutation::Enum mutations[]{ DeletionMutation::SelectionRoundTrip,
        DeletionMutation::TextUndoRoundTrip, DeletionMutation::ExternalReset };
    for(const DeletionMutation::Enum mutation : mutations){
        SCOPED_TRACE(mutation);
        hostRejected(mutation);
    }
}

TEST_F(UiSelectionDeletionTests, RejectedCommitPreservesStandaloneDeletionPrefixAndDropsFollowingEvents){
    Ui::EditModel model(m_arena, { 10u });
    UiTextEditSession session(m_arena, m_textInput);
    const UiTextEditOwner owner{ { 371u }, 3u, model.instanceGeneration() };
    ASSERT_TRUE(model.setText("abcDEFghi"));
    ASSERT_TRUE(model.setSelection(6u, 3u));
    ASSERT_EQ(session.begin(owner, model, { 30, 40, 1, 18 }), TextInputAdmission::Accepted);
    const TextInputSessionToken token = session.token();
    ASSERT_EQ(m_textInput.preedit("IME", 0u, 3u), TextInputAdmission::Accepted);
    ASSERT_EQ(session.drain(owner, model).status, UiTextEditStatus::Applied);
    ASSERT_EQ(m_textInput.preedit({}, 0u, 0u), TextInputAdmission::Accepted);
    ASSERT_EQ(m_textInput.erase(1u, 1u, 0u, TextInputDeletionBasis::Selection), TextInputAdmission::Accepted);
    ASSERT_EQ(m_textInput.commit("abcdefg"), TextInputAdmission::Accepted);
    ASSERT_EQ(m_textInput.commit("Z"), TextInputAdmission::Accepted);
    const UiTextEditResult result = session.drain(owner, model);
    EXPECT_EQ(result.status, UiTextEditStatus::ModelRejected);
    EXPECT_EQ(result.eventsApplied, 2u);
    EXPECT_TRUE(result.textChanged);
    EXPECT_EQ(model.text(), "abDEFhi");
    EXPECT_EQ(model.anchor(), 5u);
    EXPECT_EQ(model.caret(), 2u);
    EXPECT_FALSE(model.composition().active);
    EXPECT_FALSE(session.token().valid());
    EXPECT_FALSE(m_textInput.activeSession().valid());
    EXPECT_EQ(m_textInput.commitFor(token, "later"), TextInputAdmission::InvalidSession);
    EXPECT_EQ(session.drain(owner, model).status, UiTextEditStatus::Idle);
    ASSERT_TRUE(model.undo());
    EXPECT_EQ(model.text(), "abcDEFghi");
    EXPECT_EQ(model.anchor(), 6u);
    EXPECT_EQ(model.caret(), 3u);
    EXPECT_FALSE(model.canUndo());
}

TEST_F(UiSelectionDeletionTests, RejectedCommitPreservesHostDeletionPrefixAndRetiresTheCopiedNativeToken){
    ASSERT_TRUE(seed(true));
    ASSERT_TRUE(activate());
    const TextInputSessionToken token = m_textInput.activeSession();
    ASSERT_EQ(m_textInput.preedit("IME", 0u, 3u), TextInputAdmission::Accepted);
    m_host.collectNative();
    ASSERT_TRUE(frame(m_model));
    AString<Alloc::GlobalArena> oversized(m_arena);
    oversized.assign(m_model.limits().maxBytes + 1u, 'x');
    ASSERT_EQ(m_textInput.preedit({}, 0u, 0u), TextInputAdmission::Accepted);
    ASSERT_EQ(m_textInput.erase(1u, 1u, 0u, TextInputDeletionBasis::Selection), TextInputAdmission::Accepted);
    ASSERT_EQ(m_textInput.commit(oversized), TextInputAdmission::Accepted);
    ASSERT_EQ(m_textInput.commit("Z"), TextInputAdmission::Accepted);
    m_host.collectNative();
    ASSERT_TRUE(frame(m_model));
    EXPECT_TRUE(m_result.textChanged);
    EXPECT_TRUE(m_result.selectionChanged);
    EXPECT_EQ(m_model.text(), "abDEFhi");
    EXPECT_EQ(m_model.anchor(), 5u);
    EXPECT_EQ(m_model.caret(), 2u);
    EXPECT_FALSE(m_model.composition().active);
    EXPECT_NE(m_textInput.activeSession(), token);
    EXPECT_EQ(m_textInput.commitFor(token, "later"), TextInputAdmission::InvalidSession);
    ASSERT_TRUE(frame(m_model));
    EXPECT_EQ(m_model.text(), "abDEFhi");
    EXPECT_FALSE(m_result.textChanged);
    ASSERT_TRUE(m_model.undo());
    EXPECT_EQ(m_model.text(), "abcDEFghi");
    EXPECT_EQ(m_model.anchor(), 6u);
    EXPECT_EQ(m_model.caret(), 3u);
    EXPECT_FALSE(m_model.canUndo());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

