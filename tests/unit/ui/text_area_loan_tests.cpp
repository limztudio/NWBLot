// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_area_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_text_area_loan_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;
using namespace NWB::UiTextAreaTests;


class AreaMutationSource final : public ComboSource{
public:
    explicit AreaMutationSource(const u64 instance = 4301u){
        generation = instance;
        count = 5u;
    }


public:
    [[nodiscard]] virtual u64 instanceGeneration()const override{ ++metadataCalls; return generation; }

    [[nodiscard]] virtual u64 revision()const override{
        ++metadataCalls;
        if(metadataHook){
            Function<void()> hook = Move(metadataHook);
            metadataHook = {};
            ++metadataMutations;
            hook();
        }
        return contentRevision;
    }

    [[nodiscard]] virtual u64 rowCount()const override{ ++metadataCalls; return count; }

    [[nodiscard]] virtual StringView text(const u64 index)const override{
        const StringView value = ComboSource::text(index);
        if(rowHook){
            Function<void()> hook = Move(rowHook);
            rowHook = {};
            ++rowMutations;
            hook();
        }
        if(armMetadataSource){
            AreaMutationSource* source = armMetadataSource;
            armMetadataSource = nullptr;
            source->metadataHook = Move(metadataToArm);
            metadataToArm = {};
            ++arms;
        }
        return value;
    }

    void resetAllCounters()const{ resetCounters(); metadataCalls = 0u; }


public:
    mutable Function<void()> rowHook;
    mutable Function<void()> metadataHook;
    mutable Function<void()> metadataToArm;
    mutable AreaMutationSource* armMetadataSource = nullptr;
    mutable usize metadataCalls = 0u;
    mutable usize metadataMutations = 0u;
    mutable usize rowMutations = 0u;
    mutable usize arms = 0u;
};

struct AcceptedAreaFrame{
    Array<HitTarget, 16u> targets{};
    usize count = 0u;
    usize popupCount = 0u;
    u64 generation = 0u;
};

class UiTextAreaLoanTests : public TextAreaFixture{
protected:
    [[nodiscard]] static TextAreaOptions areaOptions(){
        TextAreaOptions options;
        options.height = { LayoutSizePolicy::Fixed, 96.0f };
        return options;
    }

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


protected:
    [[nodiscard]] bool acceptBaseline(){
        useHost();
        if(!m_model.setText("alpha\nbeta") || !frameArea(1u, areaOptions()))
            return false;
        m_host.publications.clear();
        m_host.publishes = 0u;
        m_host.loans = 0u;
        return true;
    }

    [[nodiscard]] bool beginParent(const u64 generation){
        return begin(generation) && m_builder.beginPopup("parent", m_parent, parentOptions());
    }

    [[nodiscard]] bool finishRoot(){ return m_context.endRoot() && m_context.finishFrame(); }

    [[nodiscard]] AcceptedAreaFrame accepted()const{
        AcceptedAreaFrame frame;
        frame.count = m_context.input().targets().size();
        frame.popupCount = m_context.input().popupCount();
        frame.generation = m_context.input().layoutGeneration();
        for(usize index = 0u; index < Min(frame.count, frame.targets.size()); ++index)
            frame.targets[index] = m_context.input().targets()[index];
        return frame;
    }

    void expectRejected(const AcceptedAreaFrame& frame){
        EXPECT_TRUE(m_builder.failed());
        EXPECT_TRUE(m_context.failed());
        EXPECT_FALSE(m_context.ready());
        EXPECT_FALSE(m_context.commitFrame(2u));
        EXPECT_EQ(m_context.input().layoutGeneration(), frame.generation);
        EXPECT_EQ(m_context.input().popupCount(), frame.popupCount);
        ASSERT_EQ(m_context.input().targets().size(), frame.count);
        ASSERT_LE(frame.count, frame.targets.size());
        for(usize index = 0u; index < frame.count; ++index){
            const HitTarget& before = frame.targets[index];
            const HitTarget& after = m_context.input().targets()[index];
            EXPECT_EQ(after.id, before.id);
            EXPECT_EQ(after.declarationGeneration, before.declarationGeneration);
            EXPECT_EQ(after.control, before.control);
            EXPECT_EQ(after.owner, before.owner);
            EXPECT_EQ(after.ownerDeclarationGeneration, before.ownerDeclarationGeneration);
            EXPECT_EQ(after.popup, before.popup);
            EXPECT_EQ(after.enabled, before.enabled);
            UiComboTests::ExpectRect(after.rectangle, before.rectangle);
            UiComboTests::ExpectRect(after.clip, before.clip);
        }
    }


protected:
    PopupState m_parent;
    PopupState m_child;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiTextAreaLoanTests, IdenticalScrollIntentRetiresAPreparedLoanWithoutChangingNavigation){
    ASSERT_TRUE(acceptBaseline());
    const AcceptedAreaFrame before = accepted();
    ASSERT_TRUE(beginArea(2u));
    ASSERT_TRUE(m_builder.textArea("area", m_model, m_state, areaOptions()).valid);
    const Point scroll = m_state.scroll();
    const u64 revision = m_state.revision();
    const EditNavigationSnapshot navigation = m_state.navigation().snapshot();
    ASSERT_TRUE(m_state.scrollTo(scroll));
    EXPECT_GT(m_state.revision(), revision);
    EXPECT_FLOAT_EQ(m_state.scroll().x, scroll.x);
    EXPECT_FLOAT_EQ(m_state.scroll().y, scroll.y);
    EXPECT_TRUE(m_state.navigation().matches(navigation));
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_EQ(m_host.publishes, 0u);
    expectRejected(before);
}

TEST_F(UiTextAreaLoanTests, IdenticalSelectionIntentRetiresAPreparedLoanWithoutATextRevision){
    ASSERT_TRUE(acceptBaseline());
    const AcceptedAreaFrame before = accepted();
    ASSERT_TRUE(beginArea(2u));
    ASSERT_TRUE(m_builder.textArea("area", m_model, m_state, areaOptions()).valid);
    const u64 revision = m_model.revision();
    const u64 selection = m_model.selectionGeneration();
    ASSERT_TRUE(m_model.setSelection(m_model.anchor(), m_model.caret()));
    EXPECT_EQ(m_model.revision(), revision);
    EXPECT_GT(m_model.selectionGeneration(), selection);
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_EQ(m_host.publishes, 0u);
    EXPECT_EQ(m_model.text(), AStringView("alpha\nbeta"));
    expectRejected(before);
}

TEST_F(UiTextAreaLoanTests, CancelledCompositionStillRetiresAPreparedLoan){
    ASSERT_TRUE(acceptBaseline());
    const AcceptedAreaFrame before = accepted();
    ASSERT_TRUE(beginArea(2u));
    ASSERT_TRUE(m_builder.textArea("area", m_model, m_state, areaOptions()).valid);
    const u64 revision = m_model.revision();
    const u64 composition = m_model.compositionGeneration();
    ASSERT_TRUE(m_model.beginComposition());
    ASSERT_TRUE(m_model.updateComposition("x\ny", 1u, 3u));
    m_model.cancelComposition();
    EXPECT_EQ(m_model.revision(), revision);
    EXPECT_GT(m_model.compositionGeneration(), composition);
    EXPECT_FALSE(m_model.composition().active);
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_EQ(m_host.publishes, 0u);
    EXPECT_EQ(m_model.text(), AStringView("alpha\nbeta"));
    expectRejected(before);
}

TEST_F(UiTextAreaLoanTests, IdenticalScrollIntentDuringHostLoanRejectsBeforePreparation){
    ASSERT_TRUE(acceptBaseline());
    const AcceptedAreaFrame before = accepted();
    const u64 revision = m_state.revision();
    const Point scroll = m_state.scroll();
    usize attempts = 0u;
    m_host.loanHook = [&](){
        ++attempts;
        EXPECT_TRUE(m_state.scrollTo(scroll));
    };
    ASSERT_TRUE(beginArea(2u));
    EXPECT_FALSE(m_builder.textArea("area", m_model, m_state, areaOptions()).valid);
    EXPECT_EQ(attempts, 1u);
    EXPECT_EQ(m_host.loans, 1u);
    EXPECT_EQ(m_host.ordinaryLoans, 0u);
    EXPECT_EQ(m_host.actionLoans, 0u);
    EXPECT_EQ(m_host.publishes, 0u);
    EXPECT_GT(m_state.revision(), revision);
    EXPECT_FLOAT_EQ(m_state.scroll().x, scroll.x);
    EXPECT_FLOAT_EQ(m_state.scroll().y, scroll.y);
    expectRejected(before);
}

TEST_F(UiTextAreaLoanTests, FirstPublicationScrollIntentRejectsBeforeTheSecondPublication){
    ASSERT_TRUE(acceptBaseline());
    const AcceptedAreaFrame before = accepted();
    EditModel secondModel(m_arena, {}, EditTextMode::Multiline);
    TextAreaState secondState;
    ASSERT_TRUE(secondModel.setText("second\narea"));
    ASSERT_TRUE(beginArea(2u));
    ASSERT_TRUE(m_builder.textArea("area", m_model, m_state, areaOptions()).valid);
    ASSERT_TRUE(m_builder.textArea("second", secondModel, secondState, areaOptions()).valid);
    Point scroll;
    u64 revision = 0u;
    EditNavigationSnapshot navigation;
    m_host.publishHook = [&](){
        scroll = m_state.scroll();
        revision = m_state.revision();
        navigation = m_state.navigation().snapshot();
        EXPECT_TRUE(m_state.scrollTo(scroll));
    };
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_EQ(m_host.publishes, 1u);
    ASSERT_EQ(m_host.publications.size(), 1u);
    EXPECT_EQ(AStringView(m_host.publications[0u].text), AStringView("alpha\nbeta"));
    EXPECT_GT(m_state.revision(), revision);
    EXPECT_TRUE(m_state.navigation().matches(navigation));
    EXPECT_FLOAT_EQ(m_state.scroll().x, scroll.x);
    EXPECT_FLOAT_EQ(m_state.scroll().y, scroll.y);
    EXPECT_EQ(secondModel.text(), AStringView("second\narea"));
    expectRejected(before);
}

TEST_F(UiTextAreaLoanTests, EndedChildLoanRejectsLaterStateIntentBeforeAnyPublication){
    ASSERT_TRUE(acceptBaseline());
    const AcceptedAreaFrame before = accepted();
    m_parent.open();
    m_child.open();
    ASSERT_TRUE(beginParent(2u));
    ASSERT_TRUE(m_builder.beginPopup("child", m_child, childOptions()));
    ASSERT_TRUE(m_builder.textArea("area", m_model, m_state, areaOptions()).valid);
    ASSERT_TRUE(m_builder.endPopup());
    EXPECT_EQ(m_host.publishes, 0u);
    const u64 revision = m_state.revision();
    ASSERT_TRUE(m_state.scrollTo(m_state.scroll()));
    EXPECT_GT(m_state.revision(), revision);
    EXPECT_FALSE(m_builder.endPopup());
    EXPECT_EQ(m_host.publishes, 0u);
    expectRejected(before);
}

TEST_F(UiTextAreaLoanTests, ParentChildModelAliasRejectsBeforeTheSecondHostLoan){
    ASSERT_TRUE(acceptBaseline());
    const AcceptedAreaFrame before = accepted();
    TextAreaState secondState;
    m_parent.open();
    m_child.open();
    ASSERT_TRUE(beginParent(2u));
    ASSERT_TRUE(m_builder.textArea("parentArea", m_model, m_state, areaOptions()).valid);
    ASSERT_TRUE(m_builder.beginPopup("child", m_child, childOptions()));
    EXPECT_FALSE(m_builder.textArea("childArea", m_model, secondState, areaOptions()).valid);
    EXPECT_EQ(m_host.loans, 1u);
    EXPECT_EQ(m_host.publishes, 0u);
    expectRejected(before);
}

TEST_F(UiTextAreaLoanTests, EndedChildSiblingStateAliasRejectsBeforeTheSecondHostLoan){
    ASSERT_TRUE(acceptBaseline());
    const AcceptedAreaFrame before = accepted();
    EditModel secondModel(m_arena, {}, EditTextMode::Multiline);
    PopupState sibling;
    m_parent.open();
    m_child.open();
    sibling.open();
    ASSERT_TRUE(beginParent(2u));
    ASSERT_TRUE(m_builder.beginPopup("child", m_child, childOptions()));
    ASSERT_TRUE(m_builder.textArea("area", m_model, m_state, areaOptions()).valid);
    ASSERT_TRUE(m_builder.endPopup());
    ASSERT_TRUE(m_builder.beginPopup("sibling", sibling, childOptions()));
    EXPECT_FALSE(m_builder.textArea("other", secondModel, m_state, areaOptions()).valid);
    EXPECT_EQ(m_host.loans, 1u);
    EXPECT_EQ(m_host.publishes, 0u);
    expectRejected(before);
}

TEST_F(UiTextAreaLoanTests, ChildSourceSameTextReplacementRejectsTheAlreadyPaintedParent){
    ASSERT_TRUE(acceptBaseline());
    const AcceptedAreaFrame before = accepted();
    AreaMutationSource source;
    ListState list;
    m_parent.open();
    m_child.open();
    ASSERT_TRUE(beginParent(2u));
    ASSERT_TRUE(m_builder.textArea("area", m_model, m_state, areaOptions()).valid);
    const u64 external = m_model.externalRevision();
    ASSERT_TRUE(m_builder.beginPopup("child", m_child, childOptions()));
    ASSERT_TRUE(m_builder.virtualList("list", source, list, listOptions()).valid);
    ASSERT_TRUE(m_builder.endPopup());
    source.rowHook = [&](){ EXPECT_TRUE(m_model.setText("alpha\nbeta")); };
    EXPECT_FALSE(m_builder.endPopup());
    EXPECT_EQ(source.rowMutations, 1u);
    EXPECT_GT(source.textCalls, 0u);
    EXPECT_GT(m_model.externalRevision(), external);
    EXPECT_EQ(m_model.text(), AStringView("alpha\nbeta"));
    EXPECT_EQ(m_host.publishes, 1u);
    expectRejected(before);
}

TEST_F(UiTextAreaLoanTests, ParentSourceStateIntentRejectsTheEndedChildBeforePublication){
    ASSERT_TRUE(acceptBaseline());
    const AcceptedAreaFrame before = accepted();
    AreaMutationSource source;
    ListState list;
    m_parent.open();
    m_child.open();
    ASSERT_TRUE(beginParent(2u));
    ASSERT_TRUE(m_builder.virtualList("list", source, list, listOptions()).valid);
    ASSERT_TRUE(m_builder.beginPopup("child", m_child, childOptions()));
    ASSERT_TRUE(m_builder.textArea("area", m_model, m_state, areaOptions()).valid);
    ASSERT_TRUE(m_builder.endPopup());
    const u64 revision = m_state.revision();
    source.rowHook = [&](){ EXPECT_TRUE(m_state.scrollTo(m_state.scroll())); };
    EXPECT_FALSE(m_builder.endPopup());
    EXPECT_EQ(source.rowMutations, 1u);
    EXPECT_GT(m_state.revision(), revision);
    EXPECT_EQ(m_host.publishes, 0u);
    expectRejected(before);
}

TEST_F(UiTextAreaLoanTests, LaterSiblingStateScrollABARetiresAnAlreadyPaintedEndedChild){
    ASSERT_TRUE(acceptBaseline());
    const AcceptedAreaFrame before = accepted();
    AreaMutationSource source;
    ListState list;
    PopupState sibling;
    m_parent.open();
    m_child.open();
    sibling.open();
    ASSERT_TRUE(beginParent(2u));
    ASSERT_TRUE(m_builder.beginPopup("child", m_child, childOptions()));
    ASSERT_TRUE(m_builder.textArea("area", m_model, m_state, areaOptions()).valid);
    ASSERT_TRUE(m_builder.endPopup());
    ASSERT_TRUE(m_builder.beginPopup("sibling", sibling, childOptions()));
    ASSERT_TRUE(m_builder.virtualList("list", source, list, listOptions()).valid);
    ASSERT_TRUE(m_builder.endPopup());
    Point scroll;
    u64 revision = 0u;
    EditNavigationSnapshot navigation;
    source.rowHook = [&](){
        scroll = m_state.scroll();
        revision = m_state.revision();
        navigation = m_state.navigation().snapshot();
        EXPECT_TRUE(m_state.scrollTo({ scroll.x + 13.0f, scroll.y + 17.0f }));
        EXPECT_TRUE(m_state.scrollTo(scroll));
    };
    EXPECT_FALSE(m_builder.endPopup());
    EXPECT_EQ(source.rowMutations, 1u);
    EXPECT_EQ(m_host.publishes, 1u);
    EXPECT_EQ(m_state.revision(), revision + 2u);
    EXPECT_FLOAT_EQ(m_state.scroll().x, scroll.x);
    EXPECT_FLOAT_EQ(m_state.scroll().y, scroll.y);
    EXPECT_TRUE(m_state.navigation().matches(navigation));
    expectRejected(before);
}

TEST_F(UiTextAreaLoanTests, FinalMetadataScrollIntentRejectsAfterParentAndChildPaint){
    ASSERT_TRUE(acceptBaseline());
    const AcceptedAreaFrame before = accepted();
    AreaMutationSource parentSource(4301u);
    AreaMutationSource childSource(4302u);
    ListState parentList;
    ListState childList;
    m_parent.open();
    m_child.open();
    ASSERT_TRUE(beginParent(2u));
    ASSERT_TRUE(m_builder.textArea("area", m_model, m_state, areaOptions()).valid);
    ASSERT_TRUE(m_builder.virtualList("list", parentSource, parentList, listOptions()).valid);
    ASSERT_TRUE(m_builder.beginPopup("child", m_child, childOptions()));
    ASSERT_TRUE(m_builder.virtualList("list", childSource, childList, listOptions()).valid);
    ASSERT_TRUE(m_builder.endPopup());
    usize publicationsAtMutation = 0u;
    u64 revision = 0u;
    childSource.armMetadataSource = &parentSource;
    childSource.metadataToArm = [&](){
        publicationsAtMutation = m_host.publishes;
        revision = m_state.revision();
        EXPECT_TRUE(m_state.scrollTo(m_state.scroll()));
    };
    EXPECT_FALSE(m_builder.endPopup());
    EXPECT_EQ(childSource.arms, 1u);
    EXPECT_EQ(parentSource.metadataMutations, 1u);
    EXPECT_GT(childSource.textCalls, 0u);
    EXPECT_EQ(publicationsAtMutation, 1u);
    EXPECT_EQ(m_host.publishes, 1u);
    EXPECT_GT(m_state.revision(), revision);
    expectRejected(before);
}

TEST_F(UiTextAreaLoanTests, ReopenedAncestorSuppressesItsOldAreaAndSourceCallbacks){
    ASSERT_TRUE(acceptBaseline());
    AreaMutationSource source;
    ListState list;
    m_parent.open();
    m_child.open();
    ASSERT_TRUE(beginParent(2u));
    ASSERT_TRUE(m_builder.beginPopup("child", m_child, childOptions()));
    ASSERT_TRUE(m_builder.textArea("area", m_model, m_state, areaOptions()).valid);
    ASSERT_TRUE(m_builder.virtualList("list", source, list, listOptions()).valid);
    ASSERT_TRUE(m_builder.endPopup());
    source.resetAllCounters();
    source.rowHook = [&](){ ADD_FAILURE() << "retired source callback"; };
    const usize loans = m_host.loans;
    m_parent.close();
    m_parent.open();
    ASSERT_TRUE(m_model.setText("external\nreplacement"));
    ASSERT_TRUE(m_state.scrollTo({ 13.0f, 17.0f }));
    ASSERT_TRUE(m_builder.endPopup());
    ASSERT_TRUE(finishRoot());
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_EQ(m_host.loans, loans);
    EXPECT_EQ(m_host.publishes, 0u);
    EXPECT_EQ(source.metadataCalls, 0u);
    EXPECT_EQ(source.textCalls, 0u);
    EXPECT_EQ(source.rowMutations, 0u);
    EXPECT_EQ(m_model.text(), AStringView("external\nreplacement"));
    EXPECT_FLOAT_EQ(m_state.scroll().x, 13.0f);
    EXPECT_FLOAT_EQ(m_state.scroll().y, 17.0f);
}

TEST_F(UiTextAreaLoanTests, SuccessfulOuterEndReleasesLoansBeforeMutationAndNextFrameReuse){
    ASSERT_TRUE(acceptBaseline());
    m_parent.open();
    m_child.open();
    ASSERT_TRUE(beginParent(2u));
    ASSERT_TRUE(m_builder.beginPopup("child", m_child, childOptions()));
    ASSERT_TRUE(m_builder.textArea("area", m_model, m_state, areaOptions()).valid);
    ASSERT_TRUE(m_builder.endPopup());
    EXPECT_EQ(m_host.publishes, 0u);
    ASSERT_TRUE(m_builder.endPopup());
    ASSERT_EQ(m_host.publications.size(), 1u);
    EXPECT_EQ(AStringView(m_host.publications[0u].text), AStringView("alpha\nbeta"));
    const EditBoxPlacement oldPlacement = m_host.publications[0u].placement;
    ASSERT_TRUE(m_model.setText("external\nreplacement"));
    ASSERT_TRUE(m_state.scrollTo({ 6.0f, 7.0f }));
    const u64 revision = m_state.revision();
    EXPECT_EQ(AStringView(m_host.publications[0u].text), AStringView("alpha\nbeta"));
    UiComboTests::ExpectRect(m_host.publications[0u].placement.bounds, oldPlacement.bounds);
    ASSERT_TRUE(finishRoot());
    ASSERT_TRUE(m_context.commitFrame(2u));
    ASSERT_TRUE(beginParent(3u));
    ASSERT_TRUE(m_builder.beginPopup("child", m_child, childOptions()));
    ASSERT_TRUE(m_builder.textArea("area", m_model, m_state, areaOptions()).valid);
    ASSERT_TRUE(m_builder.endPopup());
    ASSERT_TRUE(m_builder.endPopup());
    ASSERT_TRUE(finishRoot());
    ASSERT_TRUE(m_context.commitFrame(3u));
    EXPECT_EQ(m_host.loans, 2u);
    EXPECT_EQ(m_host.publishes, 2u);
    ASSERT_EQ(m_host.publications.size(), 2u);
    EXPECT_EQ(AStringView(m_host.publications[1u].text), AStringView("external\nreplacement"));
    EXPECT_EQ(AStringView(m_host.publications[0u].text), AStringView("alpha\nbeta"));
    EXPECT_EQ(m_state.revision(), revision);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

