// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "numeric_edit_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_numeric_edit_loan_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;
using namespace NWB::UiNumericEditTests;


class NumericMutationSource final : public ComboSource{
public:
    NumericMutationSource(){ count = 5u; }


public:
    [[nodiscard]] virtual u64 revision()const override{
        if(onRevision){
            IntegerEditModel* model = onRevision;
            onRevision = nullptr;
            mutationApplied = model->setValue(99);
        }
        return contentRevision;
    }

    [[nodiscard]] virtual StringView text(const u64 index)const override{
        const StringView result = ComboSource::text(index);
        if(onText){
            IntegerEditModel* model = onText;
            onText = nullptr;
            mutationApplied = model->setValue(99);
        }
        if(armSource){
            armSource->onRevision = armModel;
            armSource = nullptr;
        }
        return result;
    }


public:
    mutable IntegerEditModel* onText = nullptr;
    mutable IntegerEditModel* onRevision = nullptr;
    mutable NumericMutationSource* armSource = nullptr;
    IntegerEditModel* armModel = nullptr;
    mutable bool mutationApplied = false;
};

class UiNumericEditLoanTests : public NumericFixture{
protected:
    [[nodiscard]] static PopupOptions ParentOptions(){
        PopupOptions options;
        options.anchor = { 20.0f, 20.0f, 80.0f, 20.0f };
        options.size = { 340.0f, 420.0f };
        return options;
    }

    [[nodiscard]] static PopupOptions ChildOptions(){
        PopupOptions options;
        options.anchor = { 420.0f, 20.0f, 80.0f, 20.0f };
        options.size = { 340.0f, 420.0f };
        return options;
    }

    [[nodiscard]] static ListOptions ListOptions(){
        NWB::Impl::Ui::ListOptions options;
        options.height = { LayoutSizePolicy::Fixed, 140.0f };
        options.rowHeight = 24.0f;
        return options;
    }

    [[nodiscard]] bool beginParent(const u64 generation){
        return begin(generation) && m_builder.beginPopup("parent", m_parent, ParentOptions());
    }

    [[nodiscard]] bool finishRoot(){ return m_context.endRoot() && m_context.finishFrame(); }


protected:
    PopupState m_parent;
    PopupState m_child;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiNumericEditLoanTests, SameValueExternalReplacementRetiresThePreparedLoan){
    ASSERT_TRUE(m_integer.setValue(12));
    ASSERT_TRUE(beginNumeric(1u));
    ASSERT_TRUE(m_builder.integerEdit("integer", m_integer, m_integerState).edit.valid);
    ASSERT_TRUE(m_integer.setValue(12));
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_EQ(m_integer.value(), 12);
    EXPECT_EQ(m_integer.draft().text(), AStringView("12"));
    EXPECT_TRUE(m_context.failed());
}

TEST_F(UiNumericEditLoanTests, SameBytesExternalDraftReplacementRetiresThePreparedLoan){
    ASSERT_TRUE(m_integer.setDraft("42"));
    ASSERT_TRUE(beginNumeric(1u));
    ASSERT_TRUE(m_builder.integerEdit("integer", m_integer, m_integerState).edit.valid);
    ASSERT_TRUE(m_integer.setDraft("42"));
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_EQ(m_integer.value(), 0);
    EXPECT_EQ(m_integer.draft().text(), AStringView("42"));
    EXPECT_TRUE(m_context.failed());
}

TEST_F(UiNumericEditLoanTests, DirectDraftReplacementIsFencedWithoutANumericAction){
    ASSERT_TRUE(m_integer.setDraft("42"));
    ASSERT_TRUE(beginNumeric(1u));
    ASSERT_TRUE(m_builder.integerEdit("integer", m_integer, m_integerState).edit.valid);
    const u64 numericRevision = m_integer.revision();
    ASSERT_TRUE(m_integer.lendDraft().setText("42"));
    EXPECT_EQ(m_integer.revision(), numericRevision);
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_EQ(m_integer.draft().text(), AStringView("42"));
}

TEST_F(UiNumericEditLoanTests, SelectionChangeWithoutADraftRevisionRetiresThePreparedLoan){
    ASSERT_TRUE(m_integer.setValue(42));
    ASSERT_TRUE(beginNumeric(1u));
    ASSERT_TRUE(m_builder.integerEdit("integer", m_integer, m_integerState).edit.valid);
    const u64 revision = m_integer.draft().revision();
    ASSERT_TRUE(m_integer.lendDraft().setSelection(0u, 1u));
    EXPECT_EQ(m_integer.draft().revision(), revision);
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_EQ(m_integer.draft().anchor(), 0u);
    EXPECT_EQ(m_integer.draft().caret(), 1u);
}

TEST_F(UiNumericEditLoanTests, BeginAndCancelCompositionStillRetireThePreparedLoan){
    ASSERT_TRUE(beginNumeric(1u));
    ASSERT_TRUE(m_builder.integerEdit("integer", m_integer, m_integerState).edit.valid);
    const u64 revision = m_integer.draft().revision();
    ASSERT_TRUE(m_integer.lendDraft().beginComposition());
    ASSERT_TRUE(m_integer.lendDraft().updateComposition("7", 1u, 1u));
    m_integer.lendDraft().cancelComposition();
    EXPECT_EQ(m_integer.draft().revision(), revision);
    EXPECT_FALSE(m_integer.draft().composition().active);
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_EQ(m_integer.value(), 0);
}

TEST_F(UiNumericEditLoanTests, PublishMutationRejectsAndStopsLaterEditorPublication){
    useHost();
    ASSERT_TRUE(beginNumeric(1u));
    ASSERT_TRUE(m_builder.integerEdit("integer", m_integer, m_integerState).edit.valid);
    ASSERT_TRUE(m_builder.floatEdit("decimal", m_float, m_floatState).edit.valid);
    m_host.onPublishInteger = &m_integer;
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_TRUE(m_host.mutationApplied);
    EXPECT_EQ(m_host.publishes, 1u);
    EXPECT_EQ(m_integer.value(), 99);
    EXPECT_EQ(m_float.value(), 0.0);
    EXPECT_TRUE(m_context.failed());
}

TEST_F(UiNumericEditLoanTests, FloatPublishMutationRejectsAndPreservesTheNewTypedValue){
    useHost();
    ASSERT_TRUE(beginNumeric(1u));
    ASSERT_TRUE(m_builder.floatEdit("decimal", m_float, m_floatState).edit.valid);
    m_host.onPublishFloat = &m_float;
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_TRUE(m_host.mutationApplied);
    EXPECT_DOUBLE_EQ(m_float.value(), 99.0);
    EXPECT_EQ(m_float.draft().text(), AStringView("99"));
}

TEST_F(UiNumericEditLoanTests, LaterVirtualListCallbackRejectsAnAlreadyPaintedNumericEditor){
    useHost();
    NumericMutationSource source;
    ListState list;
    ASSERT_TRUE(beginNumeric(1u));
    ASSERT_TRUE(m_builder.integerEdit("integer", m_integer, m_integerState).edit.valid);
    ASSERT_TRUE(m_builder.virtualList("list", source, list, ListOptions()).valid);
    source.onText = &m_integer;
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_TRUE(source.mutationApplied);
    EXPECT_GT(source.textCalls, 0u);
    EXPECT_EQ(m_host.publishes, 1u);
    EXPECT_EQ(m_integer.value(), 99);
}

TEST_F(UiNumericEditLoanTests, NumericLoanRemainsLiveAfterItsChildPopupEnds){
    useHost();
    m_parent.open();
    m_child.open();
    ASSERT_TRUE(beginParent(1u));
    ASSERT_TRUE(m_builder.beginPopup("child", m_child, ChildOptions()));
    ASSERT_TRUE(m_builder.integerEdit("integer", m_integer, m_integerState).edit.valid);
    ASSERT_TRUE(m_builder.endPopup());
    EXPECT_EQ(m_host.publishes, 0u);
    ASSERT_TRUE(m_integer.setValue(99));
    EXPECT_FALSE(m_builder.endPopup());
    EXPECT_EQ(m_host.publishes, 0u);
    EXPECT_EQ(m_integer.value(), 99);
}

TEST_F(UiNumericEditLoanTests, ChildSourceCallbackRejectsAnAlreadyPaintedParentNumericEditor){
    useHost();
    NumericMutationSource source;
    ListState list;
    m_parent.open();
    m_child.open();
    ASSERT_TRUE(beginParent(1u));
    ASSERT_TRUE(m_builder.integerEdit("integer", m_integer, m_integerState).edit.valid);
    ASSERT_TRUE(m_builder.beginPopup("child", m_child, ChildOptions()));
    ASSERT_TRUE(m_builder.virtualList("list", source, list, ListOptions()).valid);
    ASSERT_TRUE(m_builder.endPopup());
    source.onText = &m_integer;
    EXPECT_FALSE(m_builder.endPopup());
    EXPECT_TRUE(source.mutationApplied);
    EXPECT_EQ(m_host.publishes, 1u);
    EXPECT_EQ(m_integer.value(), 99);
}

TEST_F(UiNumericEditLoanTests, ParentSourceCallbackRejectsTheEndedChildNumericLoanBeforePublication){
    useHost();
    NumericMutationSource source;
    ListState list;
    m_parent.open();
    m_child.open();
    ASSERT_TRUE(beginParent(1u));
    ASSERT_TRUE(m_builder.virtualList("list", source, list, ListOptions()).valid);
    ASSERT_TRUE(m_builder.beginPopup("child", m_child, ChildOptions()));
    ASSERT_TRUE(m_builder.integerEdit("integer", m_integer, m_integerState).edit.valid);
    ASSERT_TRUE(m_builder.endPopup());
    source.onText = &m_integer;
    EXPECT_FALSE(m_builder.endPopup());
    EXPECT_TRUE(source.mutationApplied);
    EXPECT_EQ(m_host.publishes, 0u);
    EXPECT_EQ(m_integer.value(), 99);
}

TEST_F(UiNumericEditLoanTests, FinalSourceValidationCannotMutateAParentNumericLoanAfterChildPaint){
    useHost();
    NumericMutationSource parentSource;
    NumericMutationSource childSource;
    ListState parentList;
    ListState childList;
    m_parent.open();
    m_child.open();
    ASSERT_TRUE(beginParent(1u));
    ASSERT_TRUE(m_builder.integerEdit("integer", m_integer, m_integerState).edit.valid);
    ASSERT_TRUE(m_builder.virtualList("list", parentSource, parentList, ListOptions()).valid);
    ASSERT_TRUE(m_builder.beginPopup("child", m_child, ChildOptions()));
    ASSERT_TRUE(m_builder.virtualList("list", childSource, childList, ListOptions()).valid);
    ASSERT_TRUE(m_builder.endPopup());
    childSource.armSource = &parentSource;
    childSource.armModel = &m_integer;
    EXPECT_FALSE(m_builder.endPopup());
    EXPECT_TRUE(parentSource.mutationApplied);
    EXPECT_EQ(m_host.publishes, 1u);
    EXPECT_GT(childSource.textCalls, 0u);
    EXPECT_EQ(m_integer.value(), 99);
}

TEST_F(UiNumericEditLoanTests, LaterSiblingSourceRejectsAnAlreadyPaintedEndedChildNumericLoan){
    useHost();
    NumericMutationSource source;
    ListState list;
    PopupState sibling;
    m_parent.open();
    m_child.open();
    sibling.open();
    ASSERT_TRUE(beginParent(1u));
    ASSERT_TRUE(m_builder.beginPopup("child", m_child, ChildOptions()));
    ASSERT_TRUE(m_builder.integerEdit("integer", m_integer, m_integerState).edit.valid);
    ASSERT_TRUE(m_builder.endPopup());
    ASSERT_TRUE(m_builder.beginPopup("sibling", sibling, ChildOptions()));
    ASSERT_TRUE(m_builder.virtualList("list", source, list, ListOptions()).valid);
    ASSERT_TRUE(m_builder.endPopup());
    source.onText = &m_integer;
    EXPECT_FALSE(m_builder.endPopup());
    EXPECT_TRUE(source.mutationApplied);
    EXPECT_EQ(m_host.publishes, 1u);
    EXPECT_EQ(m_integer.value(), 99);
}

TEST_F(UiNumericEditLoanTests, ClosedAncestorSuppressesNumericPublicationAndHiddenSources){
    useHost();
    NumericMutationSource source;
    ListState list;
    m_parent.open();
    m_child.open();
    ASSERT_TRUE(beginParent(1u));
    ASSERT_TRUE(m_builder.beginPopup("child", m_child, ChildOptions()));
    ASSERT_TRUE(m_builder.integerEdit("integer", m_integer, m_integerState).edit.valid);
    ASSERT_TRUE(m_builder.virtualList("list", source, list, ListOptions()).valid);
    ASSERT_TRUE(m_builder.endPopup());
    const usize loans = m_host.loans;
    source.resetCounters();
    source.onText = &m_integer;
    m_parent.close();
    ASSERT_TRUE(m_integer.setValue(42));
    ASSERT_TRUE(m_builder.endPopup());
    ASSERT_TRUE(finishRoot());
    EXPECT_EQ(m_host.loans, loans);
    EXPECT_EQ(m_host.publishes, 0u);
    EXPECT_EQ(source.textCalls, 0u);
    EXPECT_FALSE(source.mutationApplied);
    EXPECT_EQ(m_integer.value(), 42);
}

TEST_F(UiNumericEditLoanTests, SuccessfulEndReleasesNumericLoansBeforeApplicationMutation){
    useHost();
    ASSERT_TRUE(m_integer.setValue(42));
    ASSERT_TRUE(beginNumeric(1u));
    ASSERT_TRUE(m_builder.integerEdit("integer", m_integer, m_integerState).edit.valid);
    ASSERT_TRUE(acceptNumeric());
    EXPECT_EQ(AStringView(m_host.displayed), AStringView("42"));
    ASSERT_TRUE(m_integer.setValue(99));
    EXPECT_EQ(AStringView(m_host.displayed), AStringView("42"));
    ASSERT_TRUE(beginNumeric(2u));
    ASSERT_TRUE(m_builder.integerEdit("integer", m_integer, m_integerState).edit.valid);
    ASSERT_TRUE(acceptNumeric());
    EXPECT_EQ(AStringView(m_host.displayed), AStringView("99"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

