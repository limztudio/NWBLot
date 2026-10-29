// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_box_host_fixture.h"

#include <global/simd/vector.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_edit_word_selection_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using namespace UiEditBoxTestSupport;

class UiEditWordSelectionTests : public UiEditBoxHostTests{
protected:
    [[nodiscard]] b8 down(const usize byte, const u64 timestampMs, const b8 shift = false){
        return dispatch({ .timestampMs = timestampMs , .position = atByte(byte), .type = Ui::InputEventType::PrimaryDown, .shift = shift });
    }

    [[nodiscard]] b8 move(const usize byte){
        return dispatch({ .position = atByte(byte) , .type = Ui::InputEventType::PointerMove });
    }

    [[nodiscard]] b8 up(const usize byte){
        return dispatch({ .position = atByte(byte) , .type = Ui::InputEventType::PrimaryUp });
    }

    [[nodiscard]] b8 click(const usize byte, const u64 timestampMs, const b8 shift = false){
        return down(byte, timestampMs, shift) && up(byte);
    }

    [[nodiscard]] b8 clickAt(const Ui::Point point, const u64 timestampMs){
        return dispatch({ .timestampMs = timestampMs , .position = point, .type = Ui::InputEventType::PrimaryDown }) && dispatch({ .position = point , .type = Ui::InputEventType::PrimaryUp });
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiEditWordSelectionTests, RightHalfOfFinalWordGlyphSelectsItsWordRatherThanFollowingSpace){
    ASSERT_TRUE(m_model.setText("one two"));
    ASSERT_TRUE(activate());
    const SIMDVectorSingle shifted = VectorAdd(VectorSet(m_placement.textOrigin.x, m_placement.textOrigin.y, 0.0f, 0.0f), VectorSet(29.0f, 5.0f, 0.0f, 0.0f));
    const Ui::Point point{ VectorGetX(shifted), VectorGetY(shifted) };
    ASSERT_TRUE(clickAt(point, 1000u));
    ASSERT_TRUE(frame(m_model));
    ASSERT_TRUE(clickAt(point, 1200u));
    ASSERT_TRUE(frame(m_model));
    EXPECT_EQ(m_model.selectedText(), "one");
}

TEST_F(UiEditWordSelectionTests, UncapturedPointerMoveBetweenNativeClicksPreservesWordSelection){
    ASSERT_TRUE(m_model.setText("one two"));
    ASSERT_TRUE(activate());
    ASSERT_TRUE(down(1u, 1000u));
    ASSERT_TRUE(up(1u));
    EXPECT_FALSE(m_context.input().capture().valid());
    ASSERT_TRUE(move(1u));
    ASSERT_TRUE(down(1u, 1200u));
    ASSERT_TRUE(up(1u));
    ASSERT_TRUE(frame(m_model));
    EXPECT_EQ(m_model.selectedText(), "one");
}

TEST_F(UiEditWordSelectionTests, RightHalfAtHardLineEndDoesNotSelectNewline){
    Ui::EditModel multiline(m_arena, {}, Ui::EditTextMode::Multiline);
    ASSERT_TRUE(multiline.setText("one\ntwo"));
    ASSERT_TRUE(frame(multiline));
    ASSERT_TRUE(key(Core::Key::Tab));
    ASSERT_TRUE(frame(multiline));
    const SIMDVectorSingle shifted = VectorAdd(VectorSet(m_placement.textOrigin.x, m_placement.textOrigin.y, 0.0f, 0.0f), VectorSet(29.0f, 5.0f, 0.0f, 0.0f));
    const Ui::Point point{ VectorGetX(shifted), VectorGetY(shifted) };
    ASSERT_TRUE(clickAt(point, 1000u));
    ASSERT_TRUE(frame(multiline));
    ASSERT_TRUE(clickAt(point, 1200u));
    ASSERT_TRUE(frame(multiline));
    EXPECT_EQ(multiline.selectedText(), "one");
}

TEST_F(UiEditWordSelectionTests, DoubleClickDragReversesDirectionWithoutSplittingRuns){
    ASSERT_TRUE(m_model.setText("one two three"));
    ASSERT_TRUE(activate());
    ASSERT_TRUE(click(5u, 1000u));
    ASSERT_TRUE(frame(m_model));
    ASSERT_TRUE(down(5u, 1200u));
    ASSERT_TRUE(move(1u));
    ASSERT_TRUE(move(9u));
    ASSERT_TRUE(up(9u));
    ASSERT_TRUE(frame(m_model));
    EXPECT_EQ(m_model.anchor(), 4u);
    EXPECT_EQ(m_model.caret(), 13u);
    EXPECT_EQ(m_model.selectedText(), "two three");
    EXPECT_FALSE(m_model.canUndo());
}

TEST_F(UiEditWordSelectionTests, MovedFirstPressCannotTurnTheNextClickIntoWordSelection){
    ASSERT_TRUE(m_model.setText("one two"));
    ASSERT_TRUE(activate());
    ASSERT_TRUE(down(1u, 1000u));
    ASSERT_TRUE(move(3u));
    ASSERT_TRUE(up(3u));
    ASSERT_TRUE(frame(m_model));
    ASSERT_TRUE(click(1u, 1100u));
    ASSERT_TRUE(frame(m_model));
    EXPECT_EQ(m_model.anchor(), 1u);
    EXPECT_EQ(m_model.caret(), 1u);
}

TEST_F(UiEditWordSelectionTests, ExternalReplacementFencesASecondClickAgainstStaleGeometry){
    ASSERT_TRUE(m_model.setText("one two"));
    ASSERT_TRUE(activate());
    ASSERT_TRUE(click(1u, 1000u));
    ASSERT_TRUE(frame(m_model));
    ASSERT_TRUE(m_model.setText("replacement"));
    ASSERT_TRUE(click(1u, 1100u));
    ASSERT_TRUE(frame(m_model));
    EXPECT_EQ(m_model.text(), "replacement");
    EXPECT_EQ(m_model.caret(), m_model.text().size());
    EXPECT_FALSE(m_model.hasSelection());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

