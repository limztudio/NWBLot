// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "numeric_edit_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_numeric_edit_builder_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;
using namespace NWB::UiNumericEditTests;


class PlainHost final : public IEditBoxHost{
public:
    [[nodiscard]] virtual EditBoxResult edit(const WidgetState&, EditModel&, const EditBoxOptions&)override{
        ++edits;
        EditBoxResult result;
        result.valid = true;
        return result;
    }

    [[nodiscard]] virtual EditBoxResult editInPopup(const WidgetState&, EditModel&,
        const EditBoxOptions&, const PopupToken&)override{ return {}; }
    [[nodiscard]] virtual EditBoxResult editActions(const WidgetState&, EditModel&, const EditBoxOptions&,
        const PopupToken&, IEditActionSink&)override{ return {}; }
    [[nodiscard]] virtual EditBoxResult editNavigated(const WidgetState&, EditModel&, const EditBoxOptions&,
        const PopupToken&, EditNavigationState&, IEditNavigationResolver&, IEditActionSink&)override{ return {}; }

    [[nodiscard]] virtual bool publish(const WidgetState&, const EditBoxView&,
        const EditBoxPlacement&, const EditBoxOptions&)override{ return true; }


public:
    usize edits = 0u;
};

class UiNumericEditBuilderTests : public NumericFixture{};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiNumericEditBuilderTests, AHostWithoutOrderedActionsRejectsTypedEditors){
    PlainHost host;
    m_builder.setEditHost(&host);
    ASSERT_TRUE(beginNumeric(1u));
    const auto result = m_builder.integerEdit("integer", m_integer, m_integerState);
    EXPECT_FALSE(result.edit.valid);
    EXPECT_FALSE(result.numeric.valid);
    EXPECT_EQ(host.edits, 0u);
    EXPECT_TRUE(m_context.failed());
}

TEST_F(UiNumericEditBuilderTests, SubmitCommitsAtItsPositionBeforeLaterText){
    useHost();
    m_host.replace("42");
    m_host.key(Core::Key::Enter);
    m_host.text("7");
    ASSERT_TRUE(beginNumeric(1u));
    const auto result = m_builder.integerEdit("integer", m_integer, m_integerState);
    ASSERT_TRUE(result.edit.valid && result.numeric.valid);
    EXPECT_TRUE(result.edit.submitted && result.edit.textChanged);
    EXPECT_TRUE(result.numeric.committed && result.numeric.valueChanged);
    EXPECT_FALSE(result.numeric.cancelled || result.numeric.rejected);
    EXPECT_EQ(m_integer.value(), 42);
    EXPECT_EQ(m_integer.draft().text(), AStringView("427"));
    EXPECT_TRUE(m_integer.dirty());
    ASSERT_TRUE(acceptNumeric());
    EXPECT_EQ(AStringView(m_host.displayed), AStringView("427"));
    EXPECT_EQ(m_host.loans, 1u);
    EXPECT_EQ(m_host.ordinaryLoans, 0u);
}

TEST_F(UiNumericEditBuilderTests, SubmitThenCancelAggregatesActionsAndDropsRetiredText){
    useHost();
    m_host.replace("42");
    m_host.key(Core::Key::Enter);
    m_host.text("7");
    m_host.key(Core::Key::Escape);
    m_host.text("9");
    ASSERT_TRUE(beginNumeric(1u));
    const auto result = m_builder.integerEdit("integer", m_integer, m_integerState);
    ASSERT_TRUE(result.edit.valid && result.numeric.valid);
    EXPECT_TRUE(result.edit.submitted && result.edit.cancelled);
    EXPECT_TRUE(result.numeric.committed && result.numeric.cancelled && result.numeric.restored);
    EXPECT_FALSE(result.edit.focused);
    EXPECT_EQ(m_integer.value(), 42);
    EXPECT_EQ(m_integer.draft().text(), AStringView("42"));
    EXPECT_FALSE(m_integer.dirty());
    ASSERT_TRUE(acceptNumeric());
}

TEST_F(UiNumericEditBuilderTests, SuccessfulSubmitKeepsLexicalBytesSelectionAndHistory){
    useHost();
    m_host.replace(" 0042 ");
    m_host.key(Core::Key::Home);
    m_host.key(Core::Key::Enter);
    ASSERT_TRUE(beginNumeric(1u));
    const auto result = m_builder.integerEdit("integer", m_integer, m_integerState);
    ASSERT_TRUE(result.edit.valid && result.numeric.valid);
    EXPECT_TRUE(result.numeric.committed);
    EXPECT_FALSE(result.numeric.restored || result.numeric.clamped);
    EXPECT_EQ(m_integer.value(), 42);
    EXPECT_EQ(m_integer.draft().text(), AStringView(" 0042 "));
    EXPECT_EQ(m_integer.draft().caret(), 0u);
    EXPECT_TRUE(m_integer.draft().canUndo());
    EXPECT_FALSE(m_integer.dirty());
    ASSERT_TRUE(acceptNumeric());
}

TEST_F(UiNumericEditBuilderTests, IncompleteSubmitRetainsDraftAndReportsRejection){
    useHost();
    ASSERT_TRUE(m_integer.setValue(12));
    m_host.replace("-");
    m_host.key(Core::Key::Enter);
    ASSERT_TRUE(beginNumeric(1u));
    const auto result = m_builder.integerEdit("integer", m_integer, m_integerState);
    ASSERT_TRUE(result.edit.valid && result.numeric.valid);
    EXPECT_TRUE(result.edit.submitted && result.numeric.rejected);
    EXPECT_FALSE(result.numeric.committed || result.numeric.restored);
    EXPECT_EQ(m_integer.value(), 12);
    EXPECT_EQ(m_integer.status(), NumericParseStatus::Incomplete);
    EXPECT_EQ(m_integer.draft().text(), AStringView("-"));
    ASSERT_TRUE(acceptNumeric());
}

TEST_F(UiNumericEditBuilderTests, ValidBlurCommitsAndCanonicalizesBeforeSnapshot){
    useHost();
    m_host.replace(" +0042 ");
    m_host.action(EditAction::Blur);
    ASSERT_TRUE(beginNumeric(1u));
    const auto result = m_builder.integerEdit("integer", m_integer, m_integerState);
    ASSERT_TRUE(result.edit.valid && result.numeric.valid);
    EXPECT_TRUE(result.edit.blurred && result.numeric.committed && result.numeric.valueChanged);
    EXPECT_FALSE(result.numeric.restored || result.numeric.cancelled);
    EXPECT_FALSE(result.edit.focused);
    EXPECT_EQ(m_integer.draft().text(), AStringView("42"));
    ASSERT_TRUE(acceptNumeric());
    EXPECT_EQ(AStringView(m_host.displayed), AStringView("42"));
}

TEST_F(UiNumericEditBuilderTests, InvalidBlurRestoresCurrentCommittedValueBeforeSnapshot){
    useHost();
    ASSERT_TRUE(m_integer.setValue(12));
    m_host.replace("invalid");
    m_host.action(EditAction::Blur);
    ASSERT_TRUE(beginNumeric(1u));
    const auto result = m_builder.integerEdit("integer", m_integer, m_integerState);
    ASSERT_TRUE(result.edit.valid && result.numeric.valid);
    EXPECT_TRUE(result.numeric.rejected && result.numeric.restored);
    EXPECT_FALSE(result.numeric.committed || result.numeric.cancelled);
    EXPECT_EQ(m_integer.value(), 12);
    EXPECT_EQ(m_integer.draft().text(), AStringView("12"));
    ASSERT_TRUE(acceptNumeric());
    EXPECT_EQ(AStringView(m_host.displayed), AStringView("12"));
}

TEST_F(UiNumericEditBuilderTests, FloatClampUsesTheBoundsOfTheOrderedAction){
    useHost();
    m_host.replace("12.5");
    m_host.key(Core::Key::Enter);
    FloatEditOptions options;
    options.bounds = { -10.0, 10.0, NumericBoundsPolicy::Clamp };
    ASSERT_TRUE(beginNumeric(1u));
    const auto result = m_builder.floatEdit("decimal", m_float, m_floatState, options);
    ASSERT_TRUE(result.edit.valid && result.numeric.valid);
    EXPECT_TRUE(result.numeric.committed && result.numeric.valueChanged && result.numeric.clamped);
    EXPECT_FALSE(result.numeric.rejected || result.numeric.restored);
    EXPECT_DOUBLE_EQ(m_float.value(), 10.0);
    EXPECT_EQ(m_float.draft().text(), AStringView("10"));
    ASSERT_TRUE(acceptNumeric());
}

TEST_F(UiNumericEditBuilderTests, BoundsPolicyChangePreservesDraftUntilTheNextAction){
    useHost();
    ASSERT_TRUE(m_integer.setDraft("42"));
    ASSERT_TRUE(beginNumeric(1u));
    ASSERT_TRUE(m_builder.integerEdit("integer", m_integer, m_integerState).edit.valid);
    ASSERT_TRUE(acceptNumeric());
    IntegerEditOptions options;
    options.bounds = { -10, 10, NumericBoundsPolicy::Clamp };
    ASSERT_TRUE(beginNumeric(2u));
    const auto unchanged = m_builder.integerEdit("integer", m_integer, m_integerState, options);
    ASSERT_TRUE(unchanged.edit.valid && unchanged.numeric.valid);
    EXPECT_FALSE(unchanged.numeric.committed || unchanged.numeric.restored || unchanged.numeric.clamped);
    EXPECT_TRUE(unchanged.edit.focused);
    EXPECT_EQ(m_integer.value(), 0);
    EXPECT_EQ(m_integer.draft().text(), AStringView("42"));
    ASSERT_TRUE(acceptNumeric());
    m_host.key(Core::Key::Enter);
    ASSERT_TRUE(beginNumeric(3u));
    const auto committed = m_builder.integerEdit("integer", m_integer, m_integerState, options);
    ASSERT_TRUE(committed.edit.valid && committed.numeric.valid);
    EXPECT_TRUE(committed.numeric.committed && committed.numeric.clamped);
    EXPECT_EQ(m_integer.value(), 10);
    EXPECT_EQ(m_integer.draft().text(), AStringView("10"));
    ASSERT_TRUE(acceptNumeric());
}

TEST_F(UiNumericEditBuilderTests, ReadOnlySubmitDoesNotCommitOrAdvanceNumericRevision){
    useHost();
    ASSERT_TRUE(m_integer.setValue(12));
    ASSERT_TRUE(m_integer.setDraft("42"));
    const u64 revision = m_integer.revision();
    m_host.key(Core::Key::Enter);
    IntegerEditOptions options;
    options.edit.readOnly = true;
    ASSERT_TRUE(beginNumeric(1u));
    const auto result = m_builder.integerEdit("integer", m_integer, m_integerState, options);
    ASSERT_TRUE(result.edit.valid && result.numeric.valid);
    EXPECT_TRUE(result.edit.submitted);
    EXPECT_FALSE(result.numeric.committed || result.numeric.cancelled || result.numeric.restored);
    EXPECT_EQ(m_integer.revision(), revision);
    EXPECT_EQ(m_integer.value(), 12);
    EXPECT_EQ(m_integer.draft().text(), AStringView("42"));
    ASSERT_TRUE(acceptNumeric());
}

TEST_F(UiNumericEditBuilderTests, ReadOnlyBlurRestoresWithoutACommitOrUserCancel){
    useHost();
    ASSERT_TRUE(m_integer.setValue(12));
    ASSERT_TRUE(m_integer.setDraft("42"));
    m_host.action(EditAction::Blur);
    IntegerEditOptions options;
    options.edit.readOnly = true;
    ASSERT_TRUE(beginNumeric(1u));
    const auto result = m_builder.integerEdit("integer", m_integer, m_integerState, options);
    ASSERT_TRUE(result.edit.valid && result.numeric.valid);
    EXPECT_TRUE(result.numeric.restored);
    EXPECT_FALSE(result.numeric.committed || result.numeric.cancelled || result.numeric.rejected);
    EXPECT_EQ(m_integer.value(), 12);
    EXPECT_EQ(m_integer.draft().text(), AStringView("12"));
    ASSERT_TRUE(acceptNumeric());
}

TEST_F(UiNumericEditBuilderTests, DisabledAbandonRestoresBeforeIgnoredInput){
    useHost();
    ASSERT_TRUE(m_integer.setValue(12));
    ASSERT_TRUE(m_integer.setDraft("42"));
    m_host.action(EditAction::Abandon);
    m_host.text("7");
    IntegerEditOptions options;
    options.edit.enabled = false;
    ASSERT_TRUE(beginNumeric(1u));
    const auto result = m_builder.integerEdit("integer", m_integer, m_integerState, options);
    ASSERT_TRUE(result.edit.valid && result.numeric.valid);
    EXPECT_TRUE(result.edit.abandoned && result.numeric.restored);
    EXPECT_FALSE(result.edit.focused || result.numeric.committed || result.numeric.cancelled);
    EXPECT_EQ(m_integer.draft().text(), AStringView("12"));
    ASSERT_TRUE(acceptNumeric());
    const HitTarget* field = target(id("integer", "panel"));
    ASSERT_TRUE(field);
    EXPECT_FALSE(field->enabled || field->focusable);
}

TEST_F(UiNumericEditBuilderTests, PreeditOwnsEnterAndTheFirstEscape){
    useHost();
    ASSERT_TRUE(m_integer.setValue(12));
    ASSERT_TRUE(m_integer.setDraft("42"));
    m_host.preedit("7");
    m_host.key(Core::Key::Enter);
    m_host.key(Core::Key::Escape);
    ASSERT_TRUE(beginNumeric(1u));
    const auto first = m_builder.integerEdit("integer", m_integer, m_integerState);
    ASSERT_TRUE(first.edit.valid && first.numeric.valid);
    EXPECT_FALSE(first.edit.submitted || first.edit.cancelled);
    EXPECT_FALSE(first.numeric.committed || first.numeric.cancelled);
    EXPECT_FALSE(m_integer.draft().composition().active);
    EXPECT_EQ(m_integer.value(), 12);
    EXPECT_EQ(m_integer.draft().text(), AStringView("42"));
    ASSERT_TRUE(acceptNumeric());
    m_host.key(Core::Key::Escape);
    ASSERT_TRUE(beginNumeric(2u));
    const auto second = m_builder.integerEdit("integer", m_integer, m_integerState);
    ASSERT_TRUE(second.edit.valid && second.numeric.valid);
    EXPECT_TRUE(second.edit.cancelled && second.numeric.cancelled && second.numeric.restored);
    EXPECT_EQ(m_integer.draft().text(), AStringView("12"));
    ASSERT_TRUE(acceptNumeric());
}

TEST_F(UiNumericEditBuilderTests, NumericSetterDuringTheHostLoanPreservesApplicationMutationAndRejects){
    useHost();
    m_host.onLoanInteger = &m_integer;
    ASSERT_TRUE(beginNumeric(1u));
    const auto result = m_builder.integerEdit("integer", m_integer, m_integerState);
    EXPECT_TRUE(m_host.mutationApplied);
    EXPECT_FALSE(result.edit.valid || result.numeric.valid);
    EXPECT_EQ(m_integer.value(), 99);
    EXPECT_EQ(m_integer.draft().text(), AStringView("99"));
    EXPECT_TRUE(m_context.failed());
}

TEST_F(UiNumericEditBuilderTests, BuilderResetReentryDuringTheHostLoanRejects){
    useHost();
    m_host.resetBuilder = &m_builder;
    ASSERT_TRUE(beginNumeric(1u));
    const auto result = m_builder.integerEdit("integer", m_integer, m_integerState);
    EXPECT_FALSE(result.edit.valid || result.numeric.valid);
    EXPECT_TRUE(m_context.failed());
    EXPECT_EQ(m_host.loans, 1u);
}

TEST_F(UiNumericEditBuilderTests, SharedTypedModelIsRejectedBeforeTheSecondHostLoan){
    useHost();
    EditBoxState another;
    ASSERT_TRUE(beginNumeric(1u));
    ASSERT_TRUE(m_builder.integerEdit("integer", m_integer, m_integerState).edit.valid);
    const auto duplicate = m_builder.integerEdit("duplicate", m_integer, another);
    EXPECT_FALSE(duplicate.edit.valid || duplicate.numeric.valid);
    EXPECT_EQ(m_host.loans, 1u);
    EXPECT_TRUE(m_context.failed());
}

TEST_F(UiNumericEditBuilderTests, SharedEditorStateIsRejectedBeforeTheSecondHostLoan){
    useHost();
    ASSERT_TRUE(beginNumeric(1u));
    ASSERT_TRUE(m_builder.integerEdit("integer", m_integer, m_integerState).edit.valid);
    const auto duplicate = m_builder.floatEdit("decimal", m_float, m_integerState);
    EXPECT_FALSE(duplicate.edit.valid || duplicate.numeric.valid);
    EXPECT_EQ(m_host.loans, 1u);
    EXPECT_TRUE(m_context.failed());
}

TEST_F(UiNumericEditBuilderTests, InvalidBoundsRejectBeforeBorrowingTheHost){
    useHost();
    IntegerEditOptions options;
    options.bounds.minimum = 20;
    options.bounds.maximum = 10;
    ASSERT_TRUE(beginNumeric(1u));
    const auto result = m_builder.integerEdit("integer", m_integer, m_integerState, options);
    EXPECT_FALSE(result.edit.valid || result.numeric.valid);
    EXPECT_EQ(m_host.loans, 0u);
    EXPECT_TRUE(m_context.failed());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

