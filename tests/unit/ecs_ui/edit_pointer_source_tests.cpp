// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_box_host_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_ui_edit_pointer_source_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace UiEditBoxTestSupport;

class UiEditPointerSourceTests : public UiEditBoxHostTests{
protected:
    [[nodiscard]] Ui::Point atByte(const usize byte)const{
        return { m_placement.textOrigin.x + static_cast<f32>(byte) * 10.0f + 1.0f, m_placement.textOrigin.y + 5.0f };
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiEditPointerSourceTests, ForeignContactCannotMoveOrEndTheAcceptedEditorSelection){
    ASSERT_TRUE(m_model.setText("abcdef"));
    ASSERT_TRUE(activate());
    const Ui::InputSource first{ 17u, 1u };
    const Ui::InputSource second{ 17u, 2u };
    ASSERT_TRUE(dispatchPointer({ .source = first, .phase = Ui::PointerPhase::Press, .position = atByte(1u) }));
    ASSERT_TRUE(frame(m_model));
    EXPECT_EQ(m_model.caret(), 1u);
    ASSERT_TRUE(dispatchPointer({ .source = second, .phase = Ui::PointerPhase::Press, .position = atByte(5u) }));
    ASSERT_TRUE(dispatchPointer({ .source = second, .phase = Ui::PointerPhase::Move, .position = atByte(6u) }));
    ASSERT_TRUE(dispatchPointer({ .source = second, .phase = Ui::PointerPhase::Release, .position = atByte(6u) }));
    ASSERT_TRUE(frame(m_model));
    EXPECT_EQ(m_model.caret(), 1u);
    EXPECT_EQ(m_model.anchor(), 1u);
    EXPECT_EQ(m_context.input().activePointerSource(), first);
    ASSERT_TRUE(dispatchPointer({ .source = first, .phase = Ui::PointerPhase::Move, .position = atByte(4u) }));
    ASSERT_TRUE(dispatchPointer({ .source = first, .phase = Ui::PointerPhase::Release, .position = atByte(4u) }));
    ASSERT_TRUE(frame(m_model));
    EXPECT_EQ(m_model.selectedText(), "bcd");
    EXPECT_FALSE(m_context.input().activePointerSource().valid());
}

TEST_F(UiEditPointerSourceTests, TouchTimestampsCannotManufactureNativeDoubleClickWordSelection){
    ASSERT_TRUE(m_model.setText("one two"));
    ASSERT_TRUE(activate());
    const Ui::InputSource touch{ 21u, 9u };
    for(const u64 timestamp : { 1000u, 1200u }){
        ASSERT_TRUE(dispatchPointer({ .source = touch, .phase = Ui::PointerPhase::Press, .position = atByte(1u), .timestampMs = timestamp }));
        ASSERT_TRUE(dispatchPointer({ .source = touch, .phase = Ui::PointerPhase::Release, .position = atByte(1u) }));
        ASSERT_TRUE(frame(m_model));
        EXPECT_EQ(m_model.caret(), 1u);
        EXPECT_EQ(m_model.anchor(), 1u);
        EXPECT_TRUE(m_model.selectedText().empty());
    }
}

TEST_F(UiEditPointerSourceTests, ContactCanExtendSelectionWithoutAKeyboardModifier){
    ASSERT_TRUE(m_model.setText("abcdef"));
    ASSERT_TRUE(activate());
    ASSERT_TRUE(m_model.setSelection(5u, 5u));
    ASSERT_TRUE(frame(m_model));
    const Ui::InputSource touch{ 21u, 9u };
    ASSERT_TRUE(dispatchPointer({ .source = touch, .phase = Ui::PointerPhase::Press, .position = atByte(2u), .extend = true }));
    ASSERT_TRUE(dispatchPointer({ .source = touch, .phase = Ui::PointerPhase::Release, .position = atByte(2u), .extend = true }));
    ASSERT_TRUE(frame(m_model));
    EXPECT_EQ(m_model.anchor(), 5u);
    EXPECT_EQ(m_model.caret(), 2u);
    EXPECT_EQ(m_model.selectedText(), "cde");
}

TEST_F(UiEditPointerSourceTests, CancelledContactCannotEraseAnEarlierCompletedSelection){
    ASSERT_TRUE(m_model.setText("abcdef"));
    ASSERT_TRUE(activate());
    const Ui::InputSource completed{ 31u, 1u };
    const Ui::InputSource next{ 31u, 2u };
    for(const Ui::InputSource cancelled : { completed, next }){
        ASSERT_TRUE(m_model.setSelection(0u, 0u));
        ASSERT_TRUE(frame(m_model));
        ASSERT_TRUE(dispatchPointer({ .source = completed, .phase = Ui::PointerPhase::Press, .position = atByte(1u) }));
        ASSERT_TRUE(dispatchPointer({ .source = completed, .phase = Ui::PointerPhase::Release, .position = atByte(1u) }));
        ASSERT_TRUE(dispatchPointer({ .source = cancelled, .phase = Ui::PointerPhase::Press, .position = atByte(5u) }));
        ASSERT_TRUE(dispatchPointer({ .source = cancelled, .phase = Ui::PointerPhase::Cancel, .position = atByte(5u) }));
        EXPECT_EQ(m_model.caret(), 0u);
        EXPECT_FALSE(m_context.input().activePointerSource().valid());
        ASSERT_TRUE(frame(m_model));
        EXPECT_EQ(m_model.caret(), 1u);
        EXPECT_EQ(m_model.anchor(), 1u);
        EXPECT_TRUE(m_model.selectedText().empty());
        ASSERT_TRUE(dispatchPointer({ .source = cancelled, .phase = Ui::PointerPhase::Press, .position = atByte(2u) }));
        ASSERT_TRUE(dispatchPointer({ .source = cancelled, .phase = Ui::PointerPhase::Release, .position = atByte(2u) }));
        ASSERT_TRUE(frame(m_model));
        EXPECT_EQ(m_model.caret(), 2u);
        EXPECT_EQ(m_model.anchor(), 2u);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

