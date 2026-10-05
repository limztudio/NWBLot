// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_navigation_host_fixture.h"
#include "../ui/multiline_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_edit_navigation_guard_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace UiEditNavigationTestSupport;

namespace CallbackMutation{
    enum Enum : u8{ IdenticalSelection, SelectionRoundTrip, CompositionRoundTrip, IdenticalText, IdenticalColumn, ColumnRoundTrip, ResetHost, ReenterHost, FailContext };
};

class UiEditNavigationGuardTests : public UiEditNavigationHostTests{
protected:
    void rejectTarget(const Ui::EditNavigationResult target){
        ASSERT_TRUE(m_navigationModel.setText("e\xcc\x81\nxy"));
        ASSERT_TRUE(m_navigationModel.setSelection(0u, 0u));
        ASSERT_TRUE(activateNavigation());
        ASSERT_TRUE(m_navigation.setPreferredX(70.0f));
        const UiMultilineTests::MultilineSnapshot model(m_arena, m_navigationModel);
        const Ui::EditNavigationSnapshot navigation = m_navigation.snapshot();
        const u64 displayed = m_context.input().layoutGeneration();
        const usize targets = m_context.input().targets().size();
        m_resolver.forceResult = true;
        m_resolver.forcedResult = target;
        ASSERT_TRUE(key(Core::Key::Down));
        ASSERT_TRUE(commitNative("!"));
        ASSERT_FALSE(prepareNavigation());
        EXPECT_FALSE(m_result.valid);
        ASSERT_EQ(m_resolver.records.size(), 1u);
        model.expectUnchanged(m_navigationModel);
        EXPECT_TRUE(m_navigation.matches(navigation));
        EXPECT_EQ(m_context.input().layoutGeneration(), displayed);
        EXPECT_EQ(m_context.input().targets().size(), targets);
    }

    void rejectMutation(const CallbackMutation::Enum mutation){
        ASSERT_TRUE(m_navigationModel.setText("ab\ncdef"));
        ASSERT_TRUE(m_navigationModel.setSelection(1u, 1u));
        ASSERT_TRUE(activateNavigation());
        ASSERT_TRUE(m_navigation.setPreferredX(50.0f));
        const u64 displayed = m_context.input().layoutGeneration();
        const usize targets = m_context.input().targets().size();
        Ui::EditModelSnapshot callbackModel(m_arena);
        Ui::EditNavigationSnapshot callbackNavigation;
        usize callbacks = 0u;
        bool mutationAccepted = false;
        bool innerRejected = false;
        m_resolver.hook = [this, mutation, &callbackModel, &callbackNavigation, &callbacks, &mutationAccepted, &innerRejected](){
            ++callbacks;
            switch(mutation){
            case CallbackMutation::IdenticalSelection:
                mutationAccepted = m_navigationModel.setSelection(1u, 1u);
                break;
            case CallbackMutation::SelectionRoundTrip:
                mutationAccepted = m_navigationModel.setSelection(0u, 0u) && m_navigationModel.setSelection(1u, 1u);
                break;
            case CallbackMutation::CompositionRoundTrip:
                mutationAccepted = m_navigationModel.beginComposition() && m_navigationModel.updateComposition("IME", 0u, 3u);
                m_navigationModel.cancelComposition();
                break;
            case CallbackMutation::IdenticalText:
                mutationAccepted = m_navigationModel.setText("ab\ncdef") && m_navigationModel.setSelection(1u, 1u);
                break;
            case CallbackMutation::IdenticalColumn:
                mutationAccepted = m_navigation.setPreferredX(50.0f);
                break;
            case CallbackMutation::ColumnRoundTrip:
                m_navigation.reset();
                mutationAccepted = m_navigation.setPreferredX(50.0f);
                break;
            case CallbackMutation::ResetHost:
                m_host.reset();
                mutationAccepted = true;
                break;
            case CallbackMutation::ReenterHost:
                innerRejected = !m_host.editNavigated(m_widget, m_navigationModel, {}, {}, m_navigation, m_resolver, m_actions).valid;
                mutationAccepted = true;
                break;
            case CallbackMutation::FailContext:
                m_context.fail();
                mutationAccepted = true;
                break;
            }
            callbackModel.capture(m_navigationModel);
            callbackNavigation = m_navigation.snapshot();
        };
        ASSERT_TRUE(key(Core::Key::Down));
        ASSERT_TRUE(commitNative("!"));
        ASSERT_FALSE(prepareNavigation());
        EXPECT_FALSE(m_result.valid);
        EXPECT_EQ(callbacks, 1u);
        EXPECT_TRUE(mutationAccepted);
        if(mutation == CallbackMutation::ReenterHost)
            EXPECT_TRUE(innerRejected);
        ASSERT_EQ(m_resolver.records.size(), 1u);
        EXPECT_TRUE(callbackModel.matches(m_navigationModel));
        EXPECT_TRUE(m_navigation.matches(callbackNavigation));
        EXPECT_EQ(m_navigationModel.text(), "ab\ncdef");
        EXPECT_EQ(m_navigationModel.anchor(), 1u);
        EXPECT_EQ(m_navigationModel.caret(), 1u);
        EXPECT_FALSE(m_navigationModel.canUndo());
        EXPECT_EQ(m_context.input().layoutGeneration(), displayed);
        EXPECT_EQ(m_context.input().targets().size(), targets);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiEditNavigationGuardTests, UnresolvedCallbackDoesNotApplyItsPlausibleTargetOrLaterText){
    rejectTarget({ 4u, 20.0f, false });
}

TEST_F(UiEditNavigationGuardTests, OutOfRangeCallbackTargetRejectsWithoutPartialColumnPublication){
    rejectTarget({ 8u, 20.0f, true });
}

TEST_F(UiEditNavigationGuardTests, ScalarEdgeInsideGraphemeCannotBecomeACommittedCaret){
    rejectTarget({ 1u, 20.0f, true });
}

TEST_F(UiEditNavigationGuardTests, ByteInsideUtf8ScalarCannotBecomeACommittedCaret){
    rejectTarget({ 2u, 20.0f, true });
}

TEST_F(UiEditNavigationGuardTests, NonfinitePreferredColumnRejectsBeforeApplyingTheTarget){
    rejectTarget({ 4u, Limit<f32>::s_QuietNaN, true });
}

TEST_F(UiEditNavigationGuardTests, ResolverIdenticalSelectionIntentRejectsTheOuterMovement){
    rejectMutation(CallbackMutation::IdenticalSelection);
}

TEST_F(UiEditNavigationGuardTests, ResolverSelectionAwayAndBackPreservesApplicationState){
    rejectMutation(CallbackMutation::SelectionRoundTrip);
}

TEST_F(UiEditNavigationGuardTests, ResolverCompositionRoundTripRejectsTheOuterMovement){
    rejectMutation(CallbackMutation::CompositionRoundTrip);
}

TEST_F(UiEditNavigationGuardTests, ResolverExternalSameTextAssignmentRetiresTheOuterIntention){
    rejectMutation(CallbackMutation::IdenticalText);
}

TEST_F(UiEditNavigationGuardTests, ResolverAcceptedIdenticalColumnSetterRejectsTheOuterMovement){
    rejectMutation(CallbackMutation::IdenticalColumn);
}

TEST_F(UiEditNavigationGuardTests, ResolverColumnResetAndRestorePreservesItsOwnEpoch){
    rejectMutation(CallbackMutation::ColumnRoundTrip);
}

TEST_F(UiEditNavigationGuardTests, ResolverHostResetCannotDestroyBorrowedEntryStorage){
    rejectMutation(CallbackMutation::ResetHost);
}

TEST_F(UiEditNavigationGuardTests, ResolverNestedEditRejectsBeforeBorrowingTheSameEntryAgain){
    rejectMutation(CallbackMutation::ReenterHost);
}

TEST_F(UiEditNavigationGuardTests, ResolverContextFailureRejectsItsTargetAndLaterCopiedText){
    rejectMutation(CallbackMutation::FailContext);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

