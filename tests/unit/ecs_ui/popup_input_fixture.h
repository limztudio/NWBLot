// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "edit_host_fixture_support.h"

#include <tests/unit/ui/fixture_text_shaping.h>
#include <tests/unit/ui/native_input.h>

#include <impl/ecs_ui/toolkit/widgets/popup.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace EcsUiPopupInputTestDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Core;
using namespace NWB::Impl;
using UiEditHostFixtureSupport::FakeClipboard;
using UiEditHostFixtureSupport::FakeTextInput;

class UiPopupInputTests : public testing::Test{
protected:
    struct DeclaredEdit{
        Ui::WidgetState widget;
        Ui::EditBoxResult result;
    };


private:
    static constexpr AStringView s_PopupKey = "popup";
    static constexpr AStringView s_EditKey = "edit";
    static constexpr AStringView s_ChildKey = "child";


public:
    UiPopupInputTests();


protected:
    virtual void SetUp()override;

    [[nodiscard]] Expected<DeclaredEdit> declareEdit(AStringView key, const Ui::Rect& bounds, Ui::EditModel& model);
    [[nodiscard]] b8 beginPreparedFrame();
    [[nodiscard]] b8 beginBaseFrame();
    [[nodiscard]] b8 endPreparedFrame();
    [[nodiscard]] b8 prepare();
    [[nodiscard]] b8 prepareDeferred();
    [[nodiscard]] b8 prepareNested();

    [[nodiscard]] b8 nestedFrame(){ return prepareNested() && commit(); }

    [[nodiscard]] b8 commit();

    [[nodiscard]] b8 frame(){ return prepare() && commit(); }

    [[nodiscard]] Ui::InputRoutingResult dispatch(const Ui::InputEvent& event);
    void key(Core::Key::Enum value, b8 control = false);
    void focusBase();
    void focusPopup();


protected:
    Alloc::GlobalArena m_arena;
    Ui::Context m_context;
    FakeTextInput m_textInput;
    FakeClipboard m_clipboard;
    UiEditBoxHost m_host;
    Ui::EditModel m_baseModel;
    Ui::EditModel m_popupModel;
    Ui::EditModel m_childModel;
    Tests::UiFixtureText::FixtureShaper m_shaper;
    Ui::TextLayoutBuilder m_layoutBuilder;
    Ui::PopupState m_popupState;
    Ui::PopupState m_childState;
    Ui::PopupToken m_childToken;
    Ui::PopupToken m_nestedParentToken;
    Ui::WidgetState m_childWidget;
    Ui::PopupToken m_token;
    Ui::WidgetState m_baseWidget;
    Ui::WidgetState m_popupWidget;
    u64 m_generation = 0u;
    const Ui::Rect m_viewport{ 0.0f, 0.0f, 500.0f, 200.0f };
    Ui::EditBoxResult m_childResult;
    Ui::EditBoxResult m_popupResult;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

