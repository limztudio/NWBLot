// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <tests/unit/ui/native_input.h>

#include "edit_host_fixture_support.h"

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiEditBoxTestSupport{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Core;
using namespace NWB::Impl;
using UiEditHostFixtureSupport::FakeClipboard;
using UiEditHostFixtureSupport::FakeTextInput;

class UiEditBoxHostTests : public testing::Test{
public:
    UiEditBoxHostTests();


protected:
    virtual void SetUp()override{ ASSERT_TRUE(m_textInput.setFocused(true)); }

    [[nodiscard]] b8 beginEditFrame();
    [[nodiscard]] b8 prepare(Ui::EditModel& model, const Ui::EditBoxOptions& options = {}, const Ui::Rect& bounds = { 10.0f, 20.0f, 180.0f, 30.0f }, Ui::IEditActionSink* actions = nullptr);
    [[nodiscard]] b8 publishEdit(Ui::EditModel& model, const Ui::EditBoxOptions& options, const Ui::Rect& bounds, Ui::Point previousScroll = {});
    [[nodiscard]] b8 commit();

    [[nodiscard]] b8 frame(Ui::EditModel& model, const Ui::EditBoxOptions& options = {}){
        return prepare(model, options) && commit();
    }

    [[nodiscard]] Ui::Point atByte(const usize byte)const noexcept{
        return { static_cast<f32>(byte) * 10.0f + m_placement.textOrigin.x + 1.0f, m_placement.textOrigin.y + 5.0f };
    }

    [[nodiscard]] b8 dispatch(const Ui::InputEvent& event);
    [[nodiscard]] b8 dispatchPointer(const Ui::PointerInput& input);

    [[nodiscard]] b8 key(Core::Key::Enum value, b8 shift = false, b8 control = false){
        return dispatch({ .key = value, .type = Ui::InputEventType::KeyDown, .shift = shift, .control = control }) && dispatch({ .key = value, .type = Ui::InputEventType::KeyUp, .shift = shift, .control = control });
    }

    [[nodiscard]] b8 activate(){
        return frame(m_model) && key(Core::Key::Tab) && frame(m_model) && m_textInput.activeSession().valid();
    }

    [[nodiscard]] b8 emptyFrame();


protected:
    Alloc::GlobalArena m_arena;
    Ui::Context m_context;
    FakeTextInput m_textInput;
    FakeClipboard m_clipboard;
    UiEditBoxHost m_host;
    Ui::EditModel m_model;
    UiEditHostFixtureSupport::FixtureShaper m_shaper;
    Ui::TextLayoutBuilder m_layoutBuilder;
    Ui::WidgetState m_widget;
    Ui::EditBoxPlacement m_placement;
    Ui::WidgetState m_otherWidget;
    u64 m_generation = 0u;
    Ui::EditBoxResult m_result;
    b8 m_includeButton = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

