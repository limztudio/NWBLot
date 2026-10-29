// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_box_host_fixture.h"

#include <global/simd/vector.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_ui_edit_pointer_source_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace UiEditBoxTestSupport;

using UiEditPointerSourceTests = UiEditBoxHostTests;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiEditPointerSourceTests, ForeignContactCannotMoveOrEndTheAcceptedEditorSelection){
    ASSERT_TRUE(m_model.setText("abcdef"));
    ASSERT_TRUE(activate());
    const Ui::InputSource first{ 17u, 1u };
    const Ui::InputSource second{ 17u, 2u };
    ASSERT_TRUE(dispatchPointer({ .source = first, .position = atByte(1u) , .phase = Ui::PointerPhase::Press }));
    ASSERT_TRUE(frame(m_model));
    EXPECT_EQ(m_model.caret(), 1u);
    ASSERT_TRUE(dispatchPointer({ .source = second, .position = atByte(5u) , .phase = Ui::PointerPhase::Press }));
    ASSERT_TRUE(dispatchPointer({ .source = second, .position = atByte(6u) , .phase = Ui::PointerPhase::Move }));
    ASSERT_TRUE(dispatchPointer({ .source = second, .position = atByte(6u) , .phase = Ui::PointerPhase::Release }));
    ASSERT_TRUE(frame(m_model));
    EXPECT_EQ(m_model.caret(), 1u);
    EXPECT_EQ(m_model.anchor(), 1u);
    EXPECT_EQ(m_context.input().activePointerSource(), first);
    ASSERT_TRUE(dispatchPointer({ .source = first, .position = atByte(4u) , .phase = Ui::PointerPhase::Move }));
    ASSERT_TRUE(dispatchPointer({ .source = first, .position = atByte(4u) , .phase = Ui::PointerPhase::Release }));
    ASSERT_TRUE(frame(m_model));
    EXPECT_EQ(m_model.selectedText(), "bcd");
    EXPECT_FALSE(m_context.input().activePointerSource().valid());
}

TEST_F(UiEditPointerSourceTests, TouchTimestampsCannotManufactureNativeDoubleClickWordSelection){
    ASSERT_TRUE(m_model.setText("one two"));
    ASSERT_TRUE(activate());
    const Ui::InputSource touch{ 21u, 9u };
    for(const u64 timestamp : { 1000u, 1200u }){
        ASSERT_TRUE(dispatchPointer({ .source = touch, .timestampMs = timestamp , .position = atByte(1u), .phase = Ui::PointerPhase::Press }));
        ASSERT_TRUE(dispatchPointer({ .source = touch, .position = atByte(1u) , .phase = Ui::PointerPhase::Release }));
        ASSERT_TRUE(frame(m_model));
        EXPECT_EQ(m_model.caret(), 1u);
        EXPECT_EQ(m_model.anchor(), 1u);
        EXPECT_TRUE(m_model.selectedText().empty());
    }
}

TEST_F(UiEditPointerSourceTests, CancelledContactCannotEraseAnEarlierCompletedSelection){
    ASSERT_TRUE(m_model.setText("abcdef"));
    ASSERT_TRUE(activate());
    const Ui::InputSource completed{ 31u, 1u };
    const Ui::InputSource next{ 31u, 2u };
    for(const Ui::InputSource cancelled : { completed, next }){
        ASSERT_TRUE(m_model.setSelection(0u, 0u));
        ASSERT_TRUE(frame(m_model));
        ASSERT_TRUE(dispatchPointer({ .source = completed, .position = atByte(1u) , .phase = Ui::PointerPhase::Press }));
        ASSERT_TRUE(dispatchPointer({ .source = completed, .position = atByte(1u) , .phase = Ui::PointerPhase::Release }));
        ASSERT_TRUE(dispatchPointer({ .source = cancelled, .position = atByte(5u) , .phase = Ui::PointerPhase::Press }));
        ASSERT_TRUE(dispatchPointer({ .source = cancelled, .position = atByte(5u) , .phase = Ui::PointerPhase::Cancel }));
        EXPECT_EQ(m_model.caret(), 0u);
        EXPECT_FALSE(m_context.input().activePointerSource().valid());
        ASSERT_TRUE(frame(m_model));
        EXPECT_EQ(m_model.caret(), 1u);
        EXPECT_EQ(m_model.anchor(), 1u);
        EXPECT_TRUE(m_model.selectedText().empty());
        ASSERT_TRUE(dispatchPointer({ .source = cancelled, .position = atByte(2u) , .phase = Ui::PointerPhase::Press }));
        ASSERT_TRUE(dispatchPointer({ .source = cancelled, .position = atByte(2u) , .phase = Ui::PointerPhase::Release }));
        ASSERT_TRUE(frame(m_model));
        EXPECT_EQ(m_model.caret(), 2u);
        EXPECT_EQ(m_model.anchor(), 2u);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

