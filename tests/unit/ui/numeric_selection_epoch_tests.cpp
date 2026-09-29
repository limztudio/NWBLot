// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "numeric_edit_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_numeric_selection_epoch_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;
using namespace NWB::UiNumericEditTests;

namespace EpochMutation{
    enum Enum : u8{ SelectionRoundTrip, SameSelection, CompositionRoundTrip };
};

class SelectionEpochSource final : public ComboSource{
public:
    SelectionEpochSource(){ count = 5u; }


public:
    [[nodiscard]] virtual u64 revision()const override{
        if(onRevision){
            EditModel* draft = onRevision;
            onRevision = nullptr;
            mutationApplied = mutate(*draft);
        }
        return contentRevision;
    }

    [[nodiscard]] virtual StringView text(const u64 index)const override{
        const StringView result = ComboSource::text(index);
        if(onText){
            EditModel* draft = onText;
            onText = nullptr;
            mutationApplied = mutate(*draft);
        }
        if(armSource){
            armSource->onRevision = armDraft;
            armSource = nullptr;
        }
        return result;
    }


private:
    [[nodiscard]] bool mutate(EditModel& draft)const{
        const usize anchor = draft.anchor();
        const usize caret = draft.caret();
        if(mutation == EpochMutation::SelectionRoundTrip)
            return draft.setSelection(0u, 0u) && draft.setSelection(anchor, caret);
        if(mutation == EpochMutation::SameSelection)
            return draft.setSelection(anchor, caret);
        if(!draft.beginComposition() || !draft.updateComposition("IME", 0u, 3u))
            return false;
        draft.cancelComposition();
        return true;
    }


public:
    mutable EditModel* onText = nullptr;
    mutable EditModel* onRevision = nullptr;
    mutable SelectionEpochSource* armSource = nullptr;
    EditModel* armDraft = nullptr;
    EpochMutation::Enum mutation = EpochMutation::SelectionRoundTrip;
    mutable bool mutationApplied = false;
};

class UiNumericSelectionEpochTests : public NumericFixture{
protected:
    [[nodiscard]] static PopupOptions parentOptions(){
        PopupOptions options;
        options.anchor = { 20.0f, 20.0f, 80.0f, 20.0f };
        options.size = { 340.0f, 420.0f };
        return options;
    }

    [[nodiscard]] static PopupOptions childOptions(){
        PopupOptions options;
        options.anchor = { 420.0f, 20.0f, 80.0f, 20.0f };
        options.size = { 340.0f, 420.0f };
        return options;
    }

    [[nodiscard]] static ListOptions listOptions(){
        ListOptions options;
        options.height = { LayoutSizePolicy::Fixed, 140.0f };
        options.rowHeight = 24.0f;
        return options;
    }

    [[nodiscard]] bool beginParent(const u64 generation){
        return begin(generation) && m_builder.beginPopup("parent", m_parent, parentOptions());
    }


protected:
    PopupState m_parent;
    PopupState m_child;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiNumericSelectionEpochTests, LaterSourceRejectsIntegerSelectionRoundTripWithUnchangedDraftAndTypedValue){
    useHost();
    ASSERT_TRUE(m_integer.setValue(42));
    SelectionEpochSource source;
    ListState list;
    ASSERT_TRUE(beginNumeric(1u));
    ASSERT_TRUE(m_builder.integerEdit("integer", m_integer, m_integerState).edit.valid);
    ASSERT_TRUE(m_builder.virtualList("list", source, list, listOptions()).valid);
    const u64 numericRevision = m_integer.revision();
    const u64 draftRevision = m_integer.draft().revision();
    const u64 externalRevision = m_integer.draft().externalRevision();
    const u64 selectionGeneration = m_integer.draft().selectionGeneration();
    source.onText = &m_integer.lendDraft();
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_TRUE(source.mutationApplied);
    EXPECT_GT(source.textCalls, 0u);
    EXPECT_EQ(m_host.publishes, 1u);
    EXPECT_TRUE(m_context.failed());
    EXPECT_EQ(m_integer.value(), 42);
    EXPECT_EQ(m_integer.revision(), numericRevision);
    EXPECT_EQ(m_integer.draft().text(), AStringView("42"));
    EXPECT_EQ(m_integer.draft().revision(), draftRevision);
    EXPECT_EQ(m_integer.draft().externalRevision(), externalRevision);
    EXPECT_EQ(m_integer.draft().anchor(), 2u);
    EXPECT_EQ(m_integer.draft().caret(), 2u);
    EXPECT_GT(m_integer.draft().selectionGeneration(), selectionGeneration);
}

TEST_F(UiNumericSelectionEpochTests, LaterSourceRejectsFloatSelectionRoundTripWithUnchangedDraftAndTypedValue){
    useHost();
    ASSERT_TRUE(m_float.setValue(1.25));
    SelectionEpochSource source;
    ListState list;
    ASSERT_TRUE(beginNumeric(1u));
    ASSERT_TRUE(m_builder.floatEdit("decimal", m_float, m_floatState).edit.valid);
    ASSERT_TRUE(m_builder.virtualList("list", source, list, listOptions()).valid);
    const u64 numericRevision = m_float.revision();
    const u64 draftRevision = m_float.draft().revision();
    const u64 selectionGeneration = m_float.draft().selectionGeneration();
    source.onText = &m_float.lendDraft();
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_TRUE(source.mutationApplied);
    EXPECT_EQ(m_host.publishes, 1u);
    EXPECT_DOUBLE_EQ(m_float.value(), 1.25);
    EXPECT_EQ(m_float.revision(), numericRevision);
    EXPECT_EQ(m_float.draft().text(), AStringView("1.25"));
    EXPECT_EQ(m_float.draft().revision(), draftRevision);
    EXPECT_EQ(m_float.draft().anchor(), 4u);
    EXPECT_EQ(m_float.draft().caret(), 4u);
    EXPECT_GT(m_float.draft().selectionGeneration(), selectionGeneration);
}

TEST_F(UiNumericSelectionEpochTests, LaterSourceRejectsAcceptedIdenticalSelection){
    useHost();
    ASSERT_TRUE(m_integer.setValue(42));
    SelectionEpochSource source;
    source.mutation = EpochMutation::SameSelection;
    ListState list;
    ASSERT_TRUE(beginNumeric(1u));
    ASSERT_TRUE(m_builder.integerEdit("integer", m_integer, m_integerState).edit.valid);
    ASSERT_TRUE(m_builder.virtualList("list", source, list, listOptions()).valid);
    const u64 selectionGeneration = m_integer.draft().selectionGeneration();
    source.onText = &m_integer.lendDraft();
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_TRUE(source.mutationApplied);
    EXPECT_EQ(m_host.publishes, 1u);
    EXPECT_EQ(m_integer.value(), 42);
    EXPECT_EQ(m_integer.draft().anchor(), 2u);
    EXPECT_EQ(m_integer.draft().caret(), 2u);
    EXPECT_GT(m_integer.draft().selectionGeneration(), selectionGeneration);
}

TEST_F(UiNumericSelectionEpochTests, LaterSourceRejectsCompositionRoundTripWithNoActivePreedit){
    useHost();
    ASSERT_TRUE(m_integer.setValue(42));
    SelectionEpochSource source;
    source.mutation = EpochMutation::CompositionRoundTrip;
    ListState list;
    ASSERT_TRUE(beginNumeric(1u));
    ASSERT_TRUE(m_builder.integerEdit("integer", m_integer, m_integerState).edit.valid);
    ASSERT_TRUE(m_builder.virtualList("list", source, list, listOptions()).valid);
    const u64 compositionGeneration = m_integer.draft().compositionGeneration();
    source.onText = &m_integer.lendDraft();
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_TRUE(source.mutationApplied);
    EXPECT_EQ(m_host.publishes, 1u);
    EXPECT_EQ(m_integer.value(), 42);
    EXPECT_EQ(m_integer.draft().text(), AStringView("42"));
    EXPECT_FALSE(m_integer.draft().composition().active);
    EXPECT_GT(m_integer.draft().compositionGeneration(), compositionGeneration);
}

TEST_F(UiNumericSelectionEpochTests, ChildSourceRejectsAnAlreadyPaintedParentSelectionRoundTrip){
    useHost();
    ASSERT_TRUE(m_integer.setValue(42));
    SelectionEpochSource source;
    ListState list;
    m_parent.open();
    m_child.open();
    ASSERT_TRUE(beginParent(1u));
    ASSERT_TRUE(m_builder.integerEdit("integer", m_integer, m_integerState).edit.valid);
    ASSERT_TRUE(m_builder.beginPopup("child", m_child, childOptions()));
    ASSERT_TRUE(m_builder.virtualList("list", source, list, listOptions()).valid);
    ASSERT_TRUE(m_builder.endPopup());
    source.onText = &m_integer.lendDraft();
    EXPECT_FALSE(m_builder.endPopup());
    EXPECT_TRUE(source.mutationApplied);
    EXPECT_EQ(m_host.publishes, 1u);
    EXPECT_EQ(m_integer.value(), 42);
    EXPECT_EQ(m_integer.draft().anchor(), 2u);
    EXPECT_EQ(m_integer.draft().caret(), 2u);
    EXPECT_TRUE(m_context.failed());
}

TEST_F(UiNumericSelectionEpochTests, ParentSourceRejectsTheEndedChildSelectionRoundTripBeforePublication){
    useHost();
    ASSERT_TRUE(m_integer.setValue(42));
    SelectionEpochSource source;
    ListState list;
    m_parent.open();
    m_child.open();
    ASSERT_TRUE(beginParent(1u));
    ASSERT_TRUE(m_builder.virtualList("list", source, list, listOptions()).valid);
    ASSERT_TRUE(m_builder.beginPopup("child", m_child, childOptions()));
    ASSERT_TRUE(m_builder.integerEdit("integer", m_integer, m_integerState).edit.valid);
    ASSERT_TRUE(m_builder.endPopup());
    source.onText = &m_integer.lendDraft();
    EXPECT_FALSE(m_builder.endPopup());
    EXPECT_TRUE(source.mutationApplied);
    EXPECT_EQ(m_host.publishes, 0u);
    EXPECT_EQ(m_integer.value(), 42);
    EXPECT_EQ(m_integer.draft().anchor(), 2u);
    EXPECT_EQ(m_integer.draft().caret(), 2u);
}

TEST_F(UiNumericSelectionEpochTests, LaterSiblingSourceRejectsAnAlreadyPaintedFloatSelectionRoundTrip){
    useHost();
    ASSERT_TRUE(m_float.setValue(1.25));
    SelectionEpochSource source;
    ListState list;
    PopupState sibling;
    m_parent.open();
    m_child.open();
    sibling.open();
    ASSERT_TRUE(beginParent(1u));
    ASSERT_TRUE(m_builder.beginPopup("child", m_child, childOptions()));
    ASSERT_TRUE(m_builder.floatEdit("decimal", m_float, m_floatState).edit.valid);
    ASSERT_TRUE(m_builder.endPopup());
    ASSERT_TRUE(m_builder.beginPopup("sibling", sibling, childOptions()));
    ASSERT_TRUE(m_builder.virtualList("list", source, list, listOptions()).valid);
    ASSERT_TRUE(m_builder.endPopup());
    source.onText = &m_float.lendDraft();
    EXPECT_FALSE(m_builder.endPopup());
    EXPECT_TRUE(source.mutationApplied);
    EXPECT_EQ(m_host.publishes, 1u);
    EXPECT_DOUBLE_EQ(m_float.value(), 1.25);
    EXPECT_EQ(m_float.draft().anchor(), 4u);
    EXPECT_EQ(m_float.draft().caret(), 4u);
}

TEST_F(UiNumericSelectionEpochTests, FinalSourceValidationRejectsTheParentSelectionRoundTripAfterChildPaint){
    useHost();
    ASSERT_TRUE(m_integer.setValue(42));
    SelectionEpochSource parentSource;
    SelectionEpochSource childSource;
    ListState parentList;
    ListState childList;
    m_parent.open();
    m_child.open();
    ASSERT_TRUE(beginParent(1u));
    ASSERT_TRUE(m_builder.integerEdit("integer", m_integer, m_integerState).edit.valid);
    ASSERT_TRUE(m_builder.virtualList("list", parentSource, parentList, listOptions()).valid);
    ASSERT_TRUE(m_builder.beginPopup("child", m_child, childOptions()));
    ASSERT_TRUE(m_builder.virtualList("list", childSource, childList, listOptions()).valid);
    ASSERT_TRUE(m_builder.endPopup());
    childSource.armSource = &parentSource;
    childSource.armDraft = &m_integer.lendDraft();
    EXPECT_FALSE(m_builder.endPopup());
    EXPECT_TRUE(parentSource.mutationApplied);
    EXPECT_EQ(m_host.publishes, 1u);
    EXPECT_GT(childSource.textCalls, 0u);
    EXPECT_EQ(m_integer.value(), 42);
    EXPECT_EQ(m_integer.draft().anchor(), 2u);
    EXPECT_EQ(m_integer.draft().caret(), 2u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

