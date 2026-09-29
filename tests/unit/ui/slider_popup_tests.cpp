// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "slider_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_slider_popup_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;
using namespace NWB::UiSliderTests;

class UiSliderPopupTests : public SliderFixture{
protected:
    [[nodiscard]] static PopupOptions parentOptions(){
        PopupOptions popup;
        popup.anchor = { 20.0f, 20.0f, 80.0f, 20.0f };
        popup.size = { 340.0f, 360.0f };
        return popup;
    }

    [[nodiscard]] static PopupOptions childOptions(){
        PopupOptions popup;
        popup.anchor = { 420.0f, 20.0f, 80.0f, 20.0f };
        popup.size = { 340.0f, 300.0f };
        return popup;
    }


protected:
    virtual void SetUp()override{
        SliderFixture::SetUp();
        ASSERT_TRUE(m_childState.setValue(0.5));
        m_parent.open();
        m_child.open();
    }

    [[nodiscard]] WidgetId childHost()const{
        const WidgetId parent = MakeWidgetId(MakeRootId(m_root), "parent");
        return MakeWidgetId(MakeWidgetId(parent, "child"), "slider");
    }

    [[nodiscard]] bool beginParent(const u64 generation){
        return
            begin(generation) && m_builder.beginPopup("parent", m_parent, parentOptions())
            && m_builder.slider("slider", m_state, options())
        ;
    }

    [[nodiscard]] bool beginChild(){
        return
            m_builder.beginPopup("child", m_child, childOptions())
            && m_builder.slider("slider", m_childState, options())
        ;
    }

    [[nodiscard]] bool declareFamily(const u64 generation){
        return beginParent(generation) && beginChild() && m_builder.endPopup();
    }

    [[nodiscard]] bool finishParent(){
        return m_builder.endPopup() && m_context.endRoot() && m_context.finishFrame();
    }

    [[nodiscard]] bool acceptFamily(const u64 generation){
        return declareFamily(generation) && finishParent() && m_context.commitFrame(generation);
    }


protected:
    PopupState m_parent;
    PopupState m_child;
    SliderState m_childState;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiSliderPopupTests, ChildResultsRemainStagedUntilTheOutermostPopupValidatesTheCompleteFamily){
    ASSERT_TRUE(beginParent(1u));
    EXPECT_FALSE(m_state.result().valid);
    ASSERT_TRUE(beginChild());
    EXPECT_FALSE(m_childState.result().valid);
    ASSERT_TRUE(m_builder.endPopup());
    EXPECT_FALSE(m_childState.result().valid);
    EXPECT_FALSE(m_state.result().valid);
    ASSERT_TRUE(finishParent());
    EXPECT_TRUE(m_state.result().valid);
    EXPECT_TRUE(m_childState.result().valid);
    ASSERT_TRUE(m_context.commitFrame(1u));
    EXPECT_EQ(m_context.input().popupCount(), 2u);
}

TEST_F(UiSliderPopupTests, AnIdenticalIntentOnTheEndedChildRejectsBothUnpublishedResults){
    ASSERT_TRUE(acceptFamily(1u));
    const SliderAcceptedFrame displayed = accepted();
    ASSERT_TRUE(declareFamily(2u));
    const u64 token = m_childState.inputGeneration();
    ASSERT_TRUE(m_childState.setValue(0.5));
    EXPECT_FALSE(m_builder.endPopup());
    EXPECT_TRUE(m_context.failed());
    EXPECT_NE(m_childState.inputGeneration(), token);
    EXPECT_DOUBLE_EQ(m_childState.value(), 0.5);
    EXPECT_FALSE(m_state.result().valid);
    EXPECT_FALSE(m_childState.result().valid);
    EXPECT_FALSE(m_context.commitFrame(2u));
    expectAccepted(displayed, false);
}

TEST_F(UiSliderPopupTests, LaterParentLabelCallbackCannotPublishAPreviouslyPaintedChildLoan){
    ASSERT_TRUE(acceptFamily(1u));
    ASSERT_TRUE(declareFamily(2u));
    m_laterSource.arm(SliderCallbackMutation::SameValue, &m_childState);
    ASSERT_TRUE(sibling());
    EXPECT_FALSE(m_builder.endPopup());
    EXPECT_TRUE(m_context.failed());
    EXPECT_EQ(m_laterSource.m_mutations, 1u);
    EXPECT_FALSE(m_state.result().valid);
    EXPECT_FALSE(m_childState.result().valid);
    EXPECT_DOUBLE_EQ(m_childState.value(), 0.5);
}

TEST_F(UiSliderPopupTests, ChildCallbackRejectsTheEarlierParentLoanInTheFinalCallbackFreePass){
    ASSERT_TRUE(acceptFamily(1u));
    ASSERT_TRUE(beginParent(2u));
    ASSERT_TRUE(beginChild());
    m_laterSource.arm(SliderCallbackMutation::SetValue, &m_state);
    ASSERT_TRUE(sibling());
    if(m_builder.endPopup())
        EXPECT_FALSE(m_builder.endPopup());
    EXPECT_TRUE(m_context.failed());
    EXPECT_DOUBLE_EQ(m_state.value(), 0.9);
    EXPECT_FALSE(m_state.result().valid);
    EXPECT_FALSE(m_childState.result().valid);
}

TEST_F(UiSliderPopupTests, ParentAndChildCannotBorrowTheSameSliderStateAtTheSameTime){
    ASSERT_TRUE(acceptFamily(1u));
    ASSERT_TRUE(beginParent(2u));
    ASSERT_TRUE(m_builder.beginPopup("child", m_child, childOptions()));
    EXPECT_FALSE(m_builder.slider("alias", m_state, options()));
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_state.result().valid);
    EXPECT_DOUBLE_EQ(m_state.value(), 0.25);
}

TEST_F(UiSliderPopupTests, AncestorClosureBeforePaintSuppressesCallbacksAndRetiresTheOldChildDrag){
    ASSERT_TRUE(acceptFamily(1u));
    const Point origin = SliderCenter(m_childState.placement().thumb);
    EXPECT_TRUE(send({ InputEventType::PrimaryDown, origin }).pointerConsumed);
    EXPECT_TRUE(send({ InputEventType::PointerMove, { origin.x + 60.0f, origin.y } }).pointerConsumed);
    ASSERT_TRUE(beginParent(2u));
    ASSERT_TRUE(beginChild());
    m_laterSource.clearCounters();
    ASSERT_TRUE(sibling());
    m_parent.close();
    ASSERT_TRUE(m_builder.endPopup());
    ASSERT_TRUE(finishParent());
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_EQ(m_laterSource.m_calls, 0u);
    EXPECT_DOUBLE_EQ(m_childState.value(), 0.5);
    EXPECT_FALSE(m_childState.result().valid);
    EXPECT_EQ(m_context.input().popupCount(), 0u);
    EXPECT_FALSE(m_context.input().capture().valid());
    m_parent.open();
    ASSERT_TRUE(beginParent(3u));
    EXPECT_FALSE(m_builder.beginPopup("child", m_child, childOptions()));
    EXPECT_FALSE(m_child.isOpen());
    ASSERT_TRUE(finishParent());
    ASSERT_TRUE(m_context.commitFrame(3u));
    m_child.open();
    ASSERT_TRUE(acceptFamily(4u));
    EXPECT_TRUE(send({ InputEventType::PrimaryUp, { origin.x + 60.0f, origin.y } }).pointerConsumed);
    ASSERT_TRUE(acceptFamily(5u));
    EXPECT_DOUBLE_EQ(m_childState.value(), 0.5);
    EXPECT_FALSE(m_childState.result().valueChanged);
    EXPECT_FALSE(m_childState.result().dragging);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

