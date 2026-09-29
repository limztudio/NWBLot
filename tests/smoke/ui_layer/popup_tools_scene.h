// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "popup_tools_source.h"

#include <impl/ecs_ui/components.h>
#include <impl/ui/widgets/context_menu.h>
#include <impl/ui/widgets/tooltip.h>

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
    virtual ~UiPopupToolsSmokeScene()override;


public:
    [[nodiscard]] bool paint(Impl::UiPaintContext& context);
    virtual bool keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods)override;


private:
    void observeDisplay(const Impl::Ui::DisplayMetrics& display);
    void observeState();
    void paintMarkers(Impl::UiPaintContext& context)const;
    [[nodiscard]] Array<u64, 15u> values()const;
    [[nodiscard]] Impl::Ui::Rect rowBounds(u64 key)const;


private:
    Core::InputDispatcher& m_input;
    UiPopupToolsSmokeSource m_source;
    Impl::Ui::ContextMenuState m_menu;
    Impl::Ui::TooltipState m_tooltip;
    Impl::Ui::EditModel m_sentinel;
    Impl::Ui::EditBoxState m_sentinelState;
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
    bool m_sentinelFocused = false;
    bool m_enabled = true;
    bool m_displayChanged = false;
};

using SharedUiPopupToolsSmokeScene = RefCountPtr<
    RefCounter<UiPopupToolsSmokeScene>, ArenaRefDeleter<RefCounter<UiPopupToolsSmokeScene>, Core::Alloc::GlobalArena>
>;

[[nodiscard]] bool IsUiLayerPopupToolsSmokeEnabled();
[[nodiscard]] bool IsUiLayerPopupToolsSkinSmokeEnabled();
[[nodiscard]] SharedUiPopupToolsSmokeScene CreateUiPopupToolsSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

