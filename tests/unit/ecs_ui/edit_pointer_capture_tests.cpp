// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_navigation_host_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_ui_edit_pointer_capture_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace UiEditNavigationTestSupport;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiEditPointerCaptureTests : public UiEditNavigationHostTests{
protected:
    [[nodiscard]] bool publishPart(const bool gesture){
        Array<Ui::HitTarget, 2u> targets;
        bool found = false;
        for(const auto& target : m_context.input().targets()){
            if(target.id == m_widget.id){
                targets[0u] = target;
                found = true;
                break;
            }
        }
        if(!found)
            return false;
        Ui::HitTarget& editor = targets[0u];
        editor.control = { 91u, 1u, 1u };
        Ui::HitTarget& part = targets[1u];
        part.id = m_otherWidget.id;
        part.declarationGeneration = m_otherWidget.declarationGeneration;
        part.rectangle = { 180.0f, 20.0f, 10.0f, 48.0f };
        part.clip = editor.clip;
        part.paintOrder = 1u;
        part.owner = editor.id;
        part.ownerDeclarationGeneration = editor.declarationGeneration;
        part.control = editor.control;
        part.pointerGesture = gesture;
        part.activatable = !gesture;
        if(gesture)
            part.gestureReference = part.rectangle;
        // The copied router publication leaves the already accepted text geometry owned by the host.
        ++m_generation;
        if(!m_context.input().commitTargets(targets.data(), targets.size(), m_generation))
            return false;
        m_host.synchronizeFocus();
        return true;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiEditPointerCaptureTests, ScrollbarGestureFocusCannotCollapseCrossLineSelection){
    ASSERT_TRUE(m_navigationModel.setText("abcdef\nuvwxyz"));
    ASSERT_TRUE(m_navigationModel.setSelection(2u, 9u));
    ASSERT_TRUE(activateNavigation());
    ASSERT_TRUE(publishPart(true));
    ASSERT_TRUE(m_navigation.setPreferredX(40.0f));
    const auto navigation = m_navigation.snapshot();
    const u64 revision = m_navigationModel.revision();
    const u64 selection = m_navigationModel.selectionGeneration();
    ASSERT_TRUE(dispatch({ .type = Ui::InputEventType::PrimaryDown, .position = { 185.0f, 62.0f } }));
    EXPECT_EQ(m_context.input().focus(), m_widget.id);
    EXPECT_EQ(m_context.input().capture(), m_otherWidget.id);
    ASSERT_TRUE(dispatch({ .type = Ui::InputEventType::PointerMove, .position = { 450.0f, 180.0f } }));
    ASSERT_TRUE(dispatch({ .type = Ui::InputEventType::PrimaryUp, .position = { 450.0f, 180.0f } }));
    ASSERT_TRUE(navigationFrame());
    EXPECT_EQ(m_navigationModel.anchor(), 2u);
    EXPECT_EQ(m_navigationModel.caret(), 9u);
    EXPECT_EQ(m_navigationModel.revision(), revision);
    EXPECT_EQ(m_navigationModel.selectionGeneration(), selection);
    EXPECT_TRUE(m_navigation.matches(navigation));
    EXPECT_FALSE(m_navigationModel.canUndo());
    EXPECT_TRUE(m_result.focused);
}

TEST_F(UiEditPointerCaptureTests, UnfocusedTrackActivationCannotShiftExtendCrossLineSelection){
    ASSERT_TRUE(m_navigationModel.setText("abcdef\nuvwxyz"));
    ASSERT_TRUE(m_navigationModel.setSelection(2u, 9u));
    ASSERT_TRUE(activateNavigation());
    ASSERT_TRUE(focusOther());
    ASSERT_TRUE(navigationFrame());
    ASSERT_TRUE(publishPart(false));
    EXPECT_FALSE(m_context.input().focus().valid());
    const u64 selection = m_navigationModel.selectionGeneration();
    ASSERT_TRUE(click({ 185.0f, 62.0f }, true));
    EXPECT_EQ(m_context.input().focus(), m_widget.id);
    ASSERT_TRUE(navigationFrame());
    EXPECT_EQ(m_navigationModel.anchor(), 2u);
    EXPECT_EQ(m_navigationModel.caret(), 9u);
    EXPECT_EQ(m_navigationModel.selectionGeneration(), selection);
    EXPECT_EQ(m_navigationModel.text(), "abcdef\nuvwxyz");
    EXPECT_FALSE(m_navigationModel.canUndo());
    EXPECT_TRUE(m_result.focused);
}

TEST_F(UiEditPointerCaptureTests, ReadOnlyScrollbarFocusCannotChangeTextSelection){
    ASSERT_TRUE(m_navigationModel.setText("abcdef\nuvwxyz"));
    ASSERT_TRUE(m_navigationModel.setSelection(2u, 9u));
    Ui::EditBoxOptions options;
    options.readOnly = true;
    ASSERT_TRUE(activateNavigation(options));
    ASSERT_TRUE(publishPart(true));
    const u64 selection = m_navigationModel.selectionGeneration();
    ASSERT_TRUE(click({ 185.0f, 62.0f }));
    ASSERT_TRUE(navigationFrame(options));
    EXPECT_EQ(m_navigationModel.anchor(), 2u);
    EXPECT_EQ(m_navigationModel.caret(), 9u);
    EXPECT_EQ(m_navigationModel.selectionGeneration(), selection);
    EXPECT_FALSE(m_navigationModel.canUndo());
    EXPECT_TRUE(m_result.focused);
}

TEST_F(UiEditPointerCaptureTests, MainEditorCaptureStillClampsAnOutOfBoundsCrossLineDrag){
    ASSERT_TRUE(m_navigationModel.setText("ab\ncd\nxy"));
    ASSERT_TRUE(m_navigationModel.setSelection(0u, 0u));
    ASSERT_TRUE(activateNavigation());
    ASSERT_TRUE(publishPart(true));
    ASSERT_TRUE(dispatch({ .type = Ui::InputEventType::PrimaryDown, .position = { 20.0f, 26.0f } }));
    EXPECT_EQ(m_context.input().capture(), m_widget.id);
    ASSERT_TRUE(dispatch({ .type = Ui::InputEventType::PointerMove, .position = { 30.0f, 120.0f } }));
    ASSERT_TRUE(dispatch({ .type = Ui::InputEventType::PrimaryUp, .position = { 30.0f, 120.0f } }));
    ASSERT_TRUE(navigationFrame());
    EXPECT_EQ(m_navigationModel.anchor(), 1u);
    EXPECT_EQ(m_navigationModel.caret(), 8u);
    EXPECT_EQ(m_navigationModel.selectedText(), "b\ncd\nxy");
    EXPECT_FALSE(m_navigationModel.canUndo());
    EXPECT_FALSE(m_context.input().capture().valid());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

