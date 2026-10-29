// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "popup_tools_source.h"

#include <impl/ecs_ui/paint_context.h>
#include <impl/ecs_ui/toolkit/widgets/context_menu.h>
#include <impl/ecs_ui/toolkit/widgets/tooltip.h>

#include <core/alloc/general.h>
#include <core/input/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiPopupToolsSmokeScene : public Core::IInputEventHandler, NoCopy{
public:
    UiPopupToolsSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input);
    virtual ~UiPopupToolsSmokeScene()noexcept override;


public:
    [[nodiscard]] b8 paint(Impl::UiPaintContext& context);
    virtual b8 keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods)noexcept override;


private:
    [[nodiscard]] Array<u64, 15u> values()const noexcept;
    [[nodiscard]] Impl::Ui::Rect rowBounds(u64 key)const noexcept;
    void observeState();


private:
    Core::InputDispatcher& m_input;
    UiPopupToolsSmokeSource m_source;
    Impl::Ui::ContextMenuState m_menu;
    Impl::Ui::TooltipState m_tooltip;
    Impl::Ui::EditModel m_sentinel;
    Impl::Ui::EditBoxState m_sentinelState;
    Impl::Ui::ScrollPlacement m_lastList;
    Impl::Ui::PopupPlacement m_lastMenu;
    Impl::Ui::PopupPlacement m_lastTooltip;
    Impl::Ui::Rect m_lastSentinel;
    Impl::Ui::DisplayMetrics m_lastDisplay;
    Array<u64, 15u> m_lastValues{};
    u64 m_command = 0u;
    u32 m_sequence = 0u;
    u32 m_commits = 0u;
    u32 m_anchorClicks = 0u;
    u32 m_underlying = 0u;
    b8 m_sentinelFocused = false;
    b8 m_enabled = true;
    b8 m_displayChanged = false;
};

using SharedUiPopupToolsSmokeScene = RefCountPtr< RefCounter<UiPopupToolsSmokeScene>, ArenaRefDeleter<RefCounter<UiPopupToolsSmokeScene>, Core::Alloc::GlobalArena> >;

[[nodiscard]] b8 IsUiLayerPopupToolsSmokeEnabled();
[[nodiscard]] b8 IsUiLayerPopupToolsSkinSmokeEnabled();
[[nodiscard]] SharedUiPopupToolsSmokeScene CreateUiPopupToolsSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

