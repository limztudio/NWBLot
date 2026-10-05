// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "radio_group_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_radio_group_popup_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;
using namespace NWB::UiRadioGroupTests;


class UiRadioGroupPopupTests : public RadioGroupFixture{
protected:
    [[nodiscard]] static PopupOptions ParentOptions(){
        PopupOptions options;
        options.anchor = { 20.0f, 20.0f, 80.0f, 20.0f };
        options.size = { 300.0f, 280.0f };
        return options;
    }

    [[nodiscard]] static PopupOptions ChildOptions(){
        PopupOptions options;
        options.anchor = { 320.0f, 64.0f, 8.0f, 24.0f };
        options.side = PopupPlacementSide::Right;
        options.size = { 260.0f, 260.0f };
        return options;
    }


protected:
    virtual void SetUp()override{
        RadioGroupFixture::SetUp();
        m_childSource.m_generation = 2001u;
        m_laterSource.m_generation = 2101u;
        m_childState.select(20u);
        m_parent.open();
        m_child.open();
    }

    [[nodiscard]] WidgetId parentHost()const{
        return MakeWidgetId(MakeWidgetId(MakeRootId(m_root), "parent"), "radio");
    }

    [[nodiscard]] WidgetId childHost()const{
        return MakeWidgetId(MakeWidgetId(MakeWidgetId(MakeRootId(m_root), "parent"), "child"), "radio");
    }

    [[nodiscard]] bool beginParent(const u64 generation){
        if(!begin(generation) || !m_builder.beginPopup("parent", m_parent, ParentOptions()))
            return false;
        m_result = m_builder.radioGroup("radio", m_source, m_state);
        return m_result.valid;
    }

    [[nodiscard]] bool declareFamily(const u64 generation){
        if(!beginParent(generation) || !m_builder.beginPopup("child", m_child, ChildOptions()))
            return false;
        m_childResult = m_builder.radioGroup("radio", m_childSource, m_childState);
        return m_childResult.valid && m_builder.endPopup();
    }

    [[nodiscard]] bool finishParent(){
        return m_builder.endPopup() && m_context.endRoot() && m_context.finishFrame();
    }

    [[nodiscard]] bool acceptParent(const u64 generation){
        return beginParent(generation) && finishParent() && m_context.commitFrame(generation);
    }

    [[nodiscard]] bool acceptFamily(const u64 generation){
        return declareFamily(generation) && finishParent() && m_context.commitFrame(generation);
    }

    void expectSourceClosure(const RadioCallbackSite::Enum site){
        ASSERT_TRUE(acceptParent(1u));
        ASSERT_EQ(m_context.input().focus(), parentHost());
        InputEvent held;
        held.type = InputEventType::KeyDown;
        held.key = Core::Key::Right;
        ASSERT_TRUE(send(held).keyboardConsumed);
        const RadioGroupSnapshot before = m_state.snapshot();
        m_source.resetCounters();
        m_source.armClose(m_parent, site);
        ASSERT_TRUE(beginParent(2u));
        EXPECT_FALSE(m_parent.isOpen());
        EXPECT_TRUE(m_result.valid);
        EXPECT_FALSE(m_result.selectionChanged);
        EXPECT_FALSE(m_result.activated);
        EXPECT_TRUE(m_state.matches(before));
        EXPECT_EQ(m_state.selectedKey(), 10u);
        EXPECT_EQ(m_source.m_mutations, 1u);
        EXPECT_EQ(m_source.m_callbacksAfterMutation, 0u);
        EXPECT_EQ(m_source.m_textCalls, 0u);
        ASSERT_TRUE(finishParent());
        ASSERT_TRUE(m_context.commitFrame(2u));
        EXPECT_EQ(m_context.input().popupCount(), 0u);
        EXPECT_EQ(target(parentHost()), nullptr);
        m_parent.open();
        ASSERT_TRUE(acceptParent(3u));
        held.repeat = true;
        ASSERT_TRUE(send(held).keyboardConsumed);
        ASSERT_TRUE(acceptParent(4u));
        EXPECT_EQ(m_state.selectedKey(), 10u);
        EXPECT_FALSE(m_result.selectionChanged);
        EXPECT_FALSE(m_result.activated);
        held.type = InputEventType::KeyUp;
        ASSERT_TRUE(send(held).keyboardConsumed);
    }


protected:
    PopupState m_parent;
    PopupState m_child;
    RadioSource m_childSource;
    RadioSource m_laterSource;
    RadioGroupState m_childState;
    RadioGroupState m_laterState;
    RadioGroupResult m_childResult;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiRadioGroupPopupTests, ChildEndKeepsTheRadioStateLoanThroughTheOutermostPopupEnd){
    ASSERT_TRUE(acceptFamily(1u));
    const RadioAcceptedFrame displayed = accepted();
    ASSERT_TRUE(declareFamily(2u));
    const u64 token = m_childState.inputGeneration();
    m_childState.select(20u);
    EXPECT_FALSE(m_builder.endPopup());
    EXPECT_TRUE(m_context.failed());
    EXPECT_NE(m_childState.inputGeneration(), token);
    EXPECT_EQ(m_childState.selectedKey(), 20u);
    EXPECT_FALSE(m_context.commitFrame(2u));
    // Discarding a rejected popup candidate may retire displayed popup focus/scopes, but never replaces its layout.
    expectAccepted(displayed, false, false);
}

TEST_F(UiRadioGroupPopupTests, ParentLabelCallbackRejectsAnIdenticalIntentOnTheAlreadyEndedChild){
    ASSERT_TRUE(acceptFamily(1u));
    const RadioAcceptedFrame displayed = accepted();
    ASSERT_TRUE(declareFamily(2u));
    const u64 token = m_childState.inputGeneration();
    m_laterSource.armSelection(m_childState, RadioCallbackSite::Text, 20u);
    ASSERT_TRUE(m_builder.radioGroup("later", m_laterSource, m_laterState).valid);
    EXPECT_FALSE(m_builder.endPopup());
    EXPECT_TRUE(m_context.failed());
    EXPECT_NE(m_childState.inputGeneration(), token);
    EXPECT_EQ(m_childState.selectedKey(), 20u);
    EXPECT_EQ(m_laterSource.m_mutations, 1u);
    expectAccepted(displayed, false, false);
}

TEST_F(UiRadioGroupPopupTests, ChildMetadataMutationCannotPublishTheAlreadyPaintedParentGroup){
    ASSERT_TRUE(acceptFamily(1u));
    const RadioAcceptedFrame displayed = accepted();
    ASSERT_TRUE(declareFamily(2u));
    m_childSource.armSelection(m_state, RadioCallbackSite::Revision, 50u);
    EXPECT_FALSE(m_builder.endPopup());
    EXPECT_TRUE(m_context.failed());
    EXPECT_EQ(m_state.selectedKey(), 50u);
    EXPECT_EQ(m_childSource.m_mutations, 1u);
    EXPECT_FALSE(m_context.commitFrame(2u));
    expectAccepted(displayed, false, false);
}

TEST_F(UiRadioGroupPopupTests, AParentStateCannotBeAliasedByItsLiveChildRadioGroup){
    ASSERT_TRUE(acceptFamily(1u));
    const RadioAcceptedFrame displayed = accepted();
    ASSERT_TRUE(beginParent(2u));
    ASSERT_TRUE(m_builder.beginPopup("child", m_child, ChildOptions()));
    m_childSource.resetCounters();
    EXPECT_FALSE(m_builder.radioGroup("radio", m_childSource, m_state).valid);
    EXPECT_TRUE(m_context.failed());
    EXPECT_EQ(m_childSource.m_calls, 0u);
    EXPECT_EQ(m_state.selectedKey(), 10u);
    expectAccepted(displayed, false, false);
}

TEST_F(UiRadioGroupPopupTests, AncestorClosureSuppressesMetadataAndRetiresCaptureBeforeFreshChildReuse){
    ASSERT_TRUE(acceptFamily(1u));
    ASSERT_TRUE(declareFamily(2u));
    const RadioGroupChoicePlacement* row = choice(20u, m_childState);
    ASSERT_NE(row, nullptr);
    const Point point = RadioCenter(row->rectangle);
    EXPECT_TRUE(send({ InputEventType::PrimaryDown, point }).pointerConsumed);
    EXPECT_TRUE(m_context.input().capture().valid());
    m_source.resetCounters();
    m_childSource.resetCounters();
    m_parent.close();
    ASSERT_TRUE(finishParent());
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_EQ(m_source.m_calls, 0u);
    EXPECT_EQ(m_childSource.m_calls, 0u);
    EXPECT_EQ(m_context.input().popupCount(), 0u);
    EXPECT_FALSE(m_context.input().capture().valid());
    EXPECT_EQ(m_childState.selectedKey(), 20u);
    m_parent.open();
    ASSERT_TRUE(beginParent(3u));
    EXPECT_FALSE(m_builder.beginPopup("child", m_child, ChildOptions()));
    EXPECT_FALSE(m_child.isOpen());
    ASSERT_TRUE(finishParent());
    ASSERT_TRUE(m_context.commitFrame(3u));
    m_child.open();
    ASSERT_TRUE(acceptFamily(4u));
    EXPECT_TRUE(send({ InputEventType::PrimaryUp, point }).pointerConsumed);
    ASSERT_TRUE(acceptFamily(5u));
    EXPECT_EQ(m_childState.selectedKey(), 20u);
    EXPECT_FALSE(m_childResult.activated);
}

TEST_F(UiRadioGroupPopupTests, InstanceCallbackClosingThePopupRetiresQueuedRightBeforeReconcile){
    expectSourceClosure(RadioCallbackSite::Instance);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

