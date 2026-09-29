// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "radio_group_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_radio_group_loan_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;
using namespace NWB::UiRadioGroupTests;

class UiRadioGroupLoanTests : public RadioGroupFixture{};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiRadioGroupLoanTests, IdenticalSelectionInAKeyCallbackRejectsBeforeTheNextBorrowedCall){
    ASSERT_TRUE(accept(1u));
    const RadioAcceptedFrame displayed = accepted();
    const u64 token = m_state.inputGeneration();
    m_source.resetCounters();
    m_source.armSelection(m_state, RadioCallbackSite::Key, 10u);
    EXPECT_FALSE(declare(2u));
    EXPECT_TRUE(m_context.failed());
    EXPECT_EQ(m_state.selectedKey(), 10u);
    EXPECT_NE(m_state.inputGeneration(), token);
    EXPECT_EQ(m_source.m_mutations, 1u);
    EXPECT_EQ(m_source.m_callbacksAfterMutation, 0u);
    EXPECT_EQ(m_source.m_textCalls, 0u);
    EXPECT_FALSE(m_context.commitFrame(2u));
    expectAccepted(displayed);
}

TEST_F(UiRadioGroupLoanTests, SelectionAwayAndBackInEnabledCallbackCannotPassTheSnapshotFence){
    ASSERT_TRUE(accept(1u));
    const RadioAcceptedFrame displayed = accepted();
    const RadioGroupSnapshot saved = m_state.snapshot();
    m_source.resetCounters();
    m_source.armSelection(m_state, RadioCallbackSite::Enabled, 10u, true);
    EXPECT_FALSE(declare(2u));
    EXPECT_TRUE(m_context.failed());
    EXPECT_EQ(m_state.selectedKey(), 10u);
    EXPECT_EQ(m_state.cursorKey(), 10u);
    EXPECT_FALSE(m_state.matches(saved));
    EXPECT_EQ(m_source.m_mutations, 1u);
    EXPECT_EQ(m_source.m_callbacksAfterMutation, 0u);
    expectAccepted(displayed);
}

TEST_F(UiRadioGroupLoanTests, SourceRevisionChangedMidReconcileIsPreservedWithoutPublishingPartialChoices){
    ASSERT_TRUE(accept(1u));
    const RadioAcceptedFrame displayed = accepted();
    const RadioGroupSnapshot saved = m_state.snapshot();
    const u64 revision = m_source.m_contentRevision;
    m_source.armSource(RadioCallbackSite::Key);
    EXPECT_FALSE(declare(2u));
    EXPECT_TRUE(m_context.failed());
    EXPECT_EQ(m_source.m_contentRevision, revision + 1u);
    EXPECT_TRUE(m_state.matches(saved));
    expectAccepted(displayed);
}

TEST_F(UiRadioGroupLoanTests, SourceReplacementInsideALabelCallbackRetainsTheApplicationMutation){
    ASSERT_TRUE(accept(1u));
    const RadioAcceptedFrame displayed = accepted();
    const u64 generation = m_source.m_generation;
    m_source.armSource(RadioCallbackSite::Text, true);
    EXPECT_FALSE(declare(2u));
    EXPECT_TRUE(m_context.failed());
    EXPECT_EQ(m_source.m_generation, generation + 1u);
    EXPECT_EQ(m_state.selectedKey(), 10u);
    expectAccepted(displayed);
}

TEST_F(UiRadioGroupLoanTests, FinalCountCallbackRevisionMutationRejectsEvenAfterEarlierMetadataMatched){
    ASSERT_TRUE(accept(1u));
    const RadioAcceptedFrame displayed = accepted();
    ASSERT_TRUE(declare(2u));
    const RadioGroupSnapshot prepared = m_state.snapshot();
    const u64 revision = m_source.m_contentRevision;
    m_source.armSource(RadioCallbackSite::Count);
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_context.ready());
    EXPECT_EQ(m_source.m_contentRevision, revision + 1u);
    EXPECT_TRUE(m_state.matches(prepared));
    EXPECT_FALSE(m_context.commitFrame(2u));
    expectAccepted(displayed);
}

TEST_F(UiRadioGroupLoanTests, ExplicitSameValueIntentAfterDeclarationRejectsBeforeAnyFurtherSourceCall){
    ASSERT_TRUE(accept(1u));
    const RadioAcceptedFrame displayed = accepted();
    ASSERT_TRUE(declare(2u));
    m_source.resetCounters();
    m_state.select(10u);
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_TRUE(m_context.failed());
    EXPECT_EQ(m_source.m_calls, 0u);
    EXPECT_EQ(m_state.selectedKey(), 10u);
    expectAccepted(displayed);
}

TEST_F(UiRadioGroupLoanTests, SharingOneStateAcrossDeclarationsRejectsBeforeTheSecondSourceIsLent){
    ASSERT_TRUE(accept(1u));
    const RadioAcceptedFrame displayed = accepted();
    ASSERT_TRUE(declare(2u));
    RadioSource other;
    other.m_generation = 1901u;
    EXPECT_FALSE(m_builder.radioGroup("other", other, m_state).valid);
    EXPECT_TRUE(m_context.failed());
    EXPECT_EQ(other.m_calls, 0u);
    EXPECT_EQ(m_state.selectedKey(), 10u);
    expectAccepted(displayed);
}

TEST_F(UiRadioGroupLoanTests, ResetDuringInitialRevisionMetadataRejectsWithoutAnotherSourceCallback){
    ASSERT_TRUE(accept(1u));
    const RadioAcceptedFrame displayed = accepted();
    m_source.resetCounters();
    m_source.armReentry(m_builder, RadioCallbackSite::Revision);
    EXPECT_FALSE(declare(2u));
    EXPECT_TRUE(m_context.failed());
    EXPECT_EQ(m_source.m_mutations, 1u);
    EXPECT_EQ(m_source.m_callbacksAfterMutation, 0u);
    EXPECT_EQ(m_state.selectedKey(), 10u);
    expectAccepted(displayed);
}

TEST_F(UiRadioGroupLoanTests, ResetInsideAKeyCallbackCannotClearTheScopeUnderTheBorrow){
    ASSERT_TRUE(accept(1u));
    const RadioAcceptedFrame displayed = accepted();
    m_source.resetCounters();
    m_source.armReentry(m_builder, RadioCallbackSite::Key);
    EXPECT_FALSE(declare(2u));
    EXPECT_TRUE(m_context.failed());
    EXPECT_EQ(m_source.m_mutations, 1u);
    EXPECT_EQ(m_source.m_callbacksAfterMutation, 0u);
    EXPECT_EQ(m_source.m_textCalls, 0u);
    expectAccepted(displayed);
}

TEST_F(UiRadioGroupLoanTests, BalancedPanelReentryInsideEnabledCallbackRejectsBothOperations){
    ASSERT_TRUE(accept(1u));
    const RadioAcceptedFrame displayed = accepted();
    m_source.resetCounters();
    m_source.armReentry(m_builder, RadioCallbackSite::Enabled, true);
    EXPECT_FALSE(declare(2u));
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_source.m_beginAccepted);
    EXPECT_FALSE(m_source.m_endAccepted);
    EXPECT_EQ(m_source.m_mutations, 1u);
    EXPECT_EQ(m_source.m_callbacksAfterMutation, 0u);
    expectAccepted(displayed);
}

TEST_F(UiRadioGroupLoanTests, LabelCallbackResetStopsBeforeTheNextLabelOrMetadataBorrow){
    ASSERT_TRUE(accept(1u));
    const RadioAcceptedFrame displayed = accepted();
    m_source.resetCounters();
    m_source.armReentry(m_builder, RadioCallbackSite::Text);
    EXPECT_FALSE(declare(2u));
    EXPECT_TRUE(m_context.failed());
    EXPECT_EQ(m_source.m_textCalls, 1u);
    EXPECT_EQ(m_source.m_callbacksAfterMutation, 0u);
    EXPECT_EQ(m_state.selectedKey(), 10u);
    expectAccepted(displayed);
}

TEST_F(UiRadioGroupLoanTests, FinalizationCountCallbackCannotBeginAndEndAReplacementPanel){
    ASSERT_TRUE(accept(1u));
    const RadioAcceptedFrame displayed = accepted();
    ASSERT_TRUE(declare(2u));
    m_source.resetCounters();
    m_source.armReentry(m_builder, RadioCallbackSite::Count, true);
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_source.m_beginAccepted);
    EXPECT_FALSE(m_source.m_endAccepted);
    EXPECT_EQ(m_source.m_mutations, 1u);
    EXPECT_EQ(m_source.m_callbacksAfterMutation, 0u);
    EXPECT_FALSE(m_context.commitFrame(2u));
    expectAccepted(displayed);
}

TEST_F(UiRadioGroupLoanTests, CompletedScopeReleasesTheSourceBeforeAcceptedPublication){
    ASSERT_TRUE(prepare(1u));
    m_source.resetCounters();
    m_source.armReentry(m_builder, RadioCallbackSite::Revision);
    ASSERT_TRUE(m_context.commitFrame(1u));
    EXPECT_EQ(m_source.m_calls, 0u);
    EXPECT_EQ(m_source.m_mutations, 0u);
    EXPECT_FALSE(m_context.failed());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

