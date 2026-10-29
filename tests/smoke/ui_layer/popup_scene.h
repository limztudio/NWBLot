// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/paint_context.h>
#include <impl/ecs_ui/toolkit/widgets/popup.h>

#include <core/alloc/general.h>
#include <core/input/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiPopupSmokeScene : public Core::IInputEventHandler, NoCopy{
public:
    UiPopupSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input);
    virtual ~UiPopupSmokeScene()noexcept override;


public:
    [[nodiscard]] b8 paint(Impl::UiPaintContext& context);
    [[nodiscard]] b8 paintLaterRoot(Impl::UiPaintContext& context)const;
    virtual b8 keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods)noexcept override;


private:
    [[nodiscard]] Array<u32, 12u> values()const noexcept;
    void observeState();


private:
    Core::InputDispatcher& m_input;
    Impl::Ui::PopupState m_popup;
    Impl::Ui::EditModel m_popupText;
    Impl::Ui::EditModel m_outsideText;
    Impl::Ui::EditBoxState m_popupEdit;
    Impl::Ui::EditBoxState m_outsideEdit;
    Impl::Ui::PopupPlacement m_lastPlacement;
    Impl::Ui::DisplayMetrics m_lastDisplay;
    Array<u32, 12u> m_lastValues{};
    u32 m_sequence = 0u;
    u32 m_underlying = 0u;
    u32 m_selected = 0u;
    b8 m_modal = false;
    b8 m_checked = false;
    b8 m_popupFocused = false;
    b8 m_outsideFocused = false;
    b8 m_edge = false;
    b8 m_right = false;
    b8 m_visible = true;
    b8 m_displayChanged = false;
};

using SharedUiPopupSmokeScene = RefCountPtr< RefCounter<UiPopupSmokeScene>, ArenaRefDeleter<RefCounter<UiPopupSmokeScene>, Core::Alloc::GlobalArena> >;

[[nodiscard]] b8 IsUiLayerPopupSmokeEnabled();
[[nodiscard]] b8 IsUiLayerPopupSkinSmokeEnabled();
[[nodiscard]] SharedUiPopupSmokeScene CreateUiPopupSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

