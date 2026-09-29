// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_box_host_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_selection_epoch_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Core;
using namespace NWB::Impl;
using namespace UiEditBoxTestSupport;

namespace EpochMutation{
    enum Enum : u8{ SelectionRoundTrip, SameSelection, CompositionRoundTrip };
};

class UiSelectionEpochTests : public UiEditBoxHostTests{
protected:
    [[nodiscard]] UiTextEditOwner owner()const{ return { { 271u }, 3u, m_model.instanceGeneration() }; }

    [[nodiscard]] bool seedSelection(){ return m_model.setText("abcd") && m_model.setSelection(1u, 3u); }

    [[nodiscard]] bool mutate(const EpochMutation::Enum mutation){
        const usize anchor = m_model.anchor();
        const usize caret = m_model.caret();
        if(mutation == EpochMutation::SelectionRoundTrip)
            return m_model.setSelection(0u, 0u) && m_model.setSelection(anchor, caret);
        if(mutation == EpochMutation::SameSelection)
            return m_model.setSelection(anchor, caret);
        if(!m_model.beginComposition() || !m_model.updateComposition("IME", 0u, 3u))
            return false;
        m_model.cancelComposition();
        return true;
    }

    void expectSelectionPreserved(const u64 revision, const u64 externalRevision)const{
        EXPECT_EQ(m_model.text(), "abcd");
        EXPECT_EQ(m_model.revision(), revision);
        EXPECT_EQ(m_model.externalRevision(), externalRevision);
        EXPECT_EQ(m_model.anchor(), 1u);
        EXPECT_EQ(m_model.caret(), 3u);
        EXPECT_FALSE(m_model.composition().active);
        EXPECT_FALSE(m_model.canUndo());
    }

    void clipboardRejected(const EpochMutation::Enum mutation){
        const Ui::EditClipboardAction::Enum actions[]{ Ui::EditClipboardAction::Cut, Ui::EditClipboardAction::Paste };
        m_clipboard.delayed = true;
        for(const auto action : actions){
            SCOPED_TRACE(action);
            ASSERT_TRUE(seedSelection());
            UiEditClipboardController controller(m_arena, m_clipboard);
            ASSERT_EQ(controller.request(owner(), m_model, action).status, UiEditClipboardStatus::Pending);
            ASSERT_TRUE(m_clipboard.pump());
            const ClipboardRequestToken token = controller.token();
            ASSERT_EQ(m_clipboard.startedToken, token);
            const u64 revision = m_model.revision();
            const u64 externalRevision = m_model.externalRevision();
            ASSERT_TRUE(mutate(mutation));
            ASSERT_TRUE(m_clipboard.deliver(token, ClipboardStatus::Success, "replacement"));
            const auto result = controller.drain(owner(), m_model);
            EXPECT_EQ(result.status, UiEditClipboardStatus::StaleModel);
            EXPECT_FALSE(result.textChanged);
            EXPECT_FALSE(result.selectionChanged);
            EXPECT_FALSE(controller.pending());
            expectSelectionPreserved(revision, externalRevision);
            EXPECT_FALSE(m_clipboard.deliver(token, ClipboardStatus::Success, "later"));
        }
    }

    void sessionRejected(const EpochMutation::Enum mutation, const bool refresh){
        ASSERT_TRUE(seedSelection());
        UiTextEditSession session(m_arena, m_textInput);
        const TextInputRect caret{ 100, 32, 1, 18 };
        ASSERT_EQ(session.begin(owner(), m_model, caret), TextInputAdmission::Accepted);
        const TextInputSessionToken token = session.token();
        const u64 surrounding = session.surroundingRevision();
        const u64 revision = m_model.revision();
        const u64 externalRevision = m_model.externalRevision();
        ASSERT_TRUE(session.matchesPublished(owner(), m_model));
        ASSERT_EQ(m_textInput.commit("late"), TextInputAdmission::Accepted);
        ASSERT_TRUE(mutate(mutation));
        EXPECT_FALSE(session.matchesPublished(owner(), m_model));
        if(refresh){
            EXPECT_EQ(session.refresh(owner(), m_model, { 400, 320, 2, 20 }), TextInputAdmission::InvalidSession);
            EXPECT_EQ(m_textInput.nativeCaret().x, caret.x);
            EXPECT_EQ(m_textInput.nativeCaret().y, caret.y);
            EXPECT_EQ(session.surroundingRevision(), surrounding);
            EXPECT_EQ(session.token(), token);
        }
        const auto result = session.drain(owner(), m_model);
        EXPECT_EQ(result.status, UiTextEditStatus::StaleModel);
        EXPECT_EQ(result.eventsApplied, 0u);
        EXPECT_FALSE(result.textChanged);
        EXPECT_FALSE(session.token().valid());
        EXPECT_FALSE(m_textInput.activeSession().valid());
        EXPECT_EQ(m_textInput.commitFor(token, "later"), TextInputAdmission::InvalidSession);
        expectSelectionPreserved(revision, externalRevision);
    }

    void hostRejected(const EpochMutation::Enum mutation){
        ASSERT_TRUE(seedSelection());
        ASSERT_TRUE(activate());
        const TextInputSessionToken token = m_textInput.activeSession();
        const u64 revision = m_model.revision();
        const u64 externalRevision = m_model.externalRevision();
        ASSERT_EQ(m_textInput.commit("late"), TextInputAdmission::Accepted);
        m_host.collectNative();
        ASSERT_TRUE(mutate(mutation));
        const u64 selectionGeneration = m_model.selectionGeneration();
        ASSERT_TRUE(frame(m_model));
        EXPECT_FALSE(m_result.textChanged);
        EXPECT_FALSE(m_result.selectionChanged);
        EXPECT_EQ(m_model.selectionGeneration(), selectionGeneration);
        expectSelectionPreserved(revision, externalRevision);
        EXPECT_EQ(m_textInput.commitFor(token, "later"), TextInputAdmission::InvalidSession);
        EXPECT_NE(m_textInput.activeSession(), token);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiSelectionEpochTests, CompletedCutAndPasteRejectSelectionRoundTrip){
    clipboardRejected(EpochMutation::SelectionRoundTrip);
}

TEST_F(UiSelectionEpochTests, CompletedCutAndPasteRejectAcceptedIdenticalSelection){
    clipboardRejected(EpochMutation::SameSelection);
}

TEST_F(UiSelectionEpochTests, CompletedCutAndPasteRejectCompositionRoundTrip){
    clipboardRejected(EpochMutation::CompositionRoundTrip);
}

TEST_F(UiSelectionEpochTests, RejectedSelectionDoesNotRetireACompletedPaste){
    ASSERT_TRUE(seedSelection());
    UiEditClipboardController controller(m_arena, m_clipboard);
    m_clipboard.delayed = true;
    ASSERT_EQ(controller.request(owner(), m_model, Ui::EditClipboardAction::Paste).status, UiEditClipboardStatus::Pending);
    ASSERT_TRUE(m_clipboard.pump());
    const u64 selectionGeneration = m_model.selectionGeneration();
    ASSERT_FALSE(m_model.setSelection(0u, m_model.text().size() + 1u));
    EXPECT_EQ(m_model.selectionGeneration(), selectionGeneration);
    ASSERT_TRUE(m_clipboard.deliver(controller.token(), ClipboardStatus::Success, "X"));
    const auto result = controller.drain(owner(), m_model);
    EXPECT_EQ(result.status, UiEditClipboardStatus::Applied);
    EXPECT_TRUE(result.textChanged);
    EXPECT_EQ(m_model.text(), "aXd");
    ASSERT_TRUE(m_model.undo());
    EXPECT_EQ(m_model.text(), "abcd");
    EXPECT_EQ(m_model.anchor(), 1u);
    EXPECT_EQ(m_model.caret(), 3u);
}

TEST_F(UiSelectionEpochTests, StandaloneDrainRejectsSelectionRoundTrip){
    sessionRejected(EpochMutation::SelectionRoundTrip, false);
}

TEST_F(UiSelectionEpochTests, StandaloneDrainRejectsAcceptedIdenticalSelection){
    sessionRejected(EpochMutation::SameSelection, false);
}

TEST_F(UiSelectionEpochTests, StandaloneDrainRejectsCompositionRoundTrip){
    sessionRejected(EpochMutation::CompositionRoundTrip, false);
}

TEST_F(UiSelectionEpochTests, StandaloneRefreshRejectsSelectionRoundTripBeforeCaretPublication){
    sessionRejected(EpochMutation::SelectionRoundTrip, true);
}

TEST_F(UiSelectionEpochTests, StandaloneRefreshRejectsAcceptedIdenticalSelectionBeforeCaretPublication){
    sessionRejected(EpochMutation::SameSelection, true);
}

TEST_F(UiSelectionEpochTests, StandaloneRefreshRejectsCompositionRoundTripBeforeCaretPublication){
    sessionRejected(EpochMutation::CompositionRoundTrip, true);
}

TEST_F(UiSelectionEpochTests, OrderedHostRejectsCollectedNativeTextAfterSelectionRoundTrip){
    hostRejected(EpochMutation::SelectionRoundTrip);
}

TEST_F(UiSelectionEpochTests, OrderedHostRejectsCollectedNativeTextAfterAcceptedIdenticalSelection){
    hostRejected(EpochMutation::SameSelection);
}

TEST_F(UiSelectionEpochTests, OrderedHostRejectsCollectedNativeTextAfterCompositionRoundTrip){
    hostRejected(EpochMutation::CompositionRoundTrip);
}

TEST_F(UiSelectionEpochTests, OrderedHostRetiresOldNativeTextAndKeepsTheApplicationReplacementSelection){
    ASSERT_TRUE(seedSelection());
    ASSERT_TRUE(activate());
    const TextInputSessionToken token = m_textInput.activeSession();
    ASSERT_EQ(m_textInput.commit("late"), TextInputAdmission::Accepted);
    m_host.collectNative();
    ASSERT_TRUE(m_model.setSelection(3u, 0u));
    const u64 selectionGeneration = m_model.selectionGeneration();
    ASSERT_TRUE(frame(m_model));
    EXPECT_EQ(m_model.text(), "abcd");
    EXPECT_EQ(m_model.anchor(), 3u);
    EXPECT_EQ(m_model.caret(), 0u);
    EXPECT_EQ(m_model.selectionGeneration(), selectionGeneration);
    EXPECT_FALSE(m_model.canUndo());
    EXPECT_EQ(m_textInput.commitFor(token, "later"), TextInputAdmission::InvalidSession);
    ASSERT_EQ(m_textInput.commit("fresh"), TextInputAdmission::Accepted);
    m_host.collectNative();
    ASSERT_TRUE(frame(m_model));
    EXPECT_EQ(m_model.text(), "freshd");
    ASSERT_TRUE(m_model.undo());
    EXPECT_EQ(m_model.text(), "abcd");
    EXPECT_EQ(m_model.anchor(), 3u);
    EXPECT_EQ(m_model.caret(), 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

