// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "combo_source.h"

#include <impl/ecs_ui/paint_context.h>
#include <impl/ecs_ui/toolkit/widgets/combo.h>

#include <core/alloc/general.h>
#include <core/input/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiComboSmokeScene : public Core::IInputEventHandler, NoCopy{
public:
    explicit UiComboSmokeScene(Core::InputDispatcher& input);
    virtual ~UiComboSmokeScene()noexcept override;


public:
    [[nodiscard]] b8 paint(Impl::UiPaintContext& context);
    virtual b8 keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods)noexcept override;


private:
    [[nodiscard]] Array<u64, 16u> values()const noexcept;
    [[nodiscard]] Impl::Ui::Rect cursorRow()const noexcept;
    void observeState();


private:
    Core::InputDispatcher& m_input;
    UiComboSmokeSource m_source;
    Impl::Ui::ComboState m_state;
    Impl::Ui::ScrollPlacement m_lastPlacement;
    Array<u64, 16u> m_lastValues{};
    Impl::Ui::PopupPlacement m_lastPopup;
    Impl::Ui::Rect m_lastBounds;
    Impl::Ui::DisplayMetrics m_lastDisplay;
    u32 m_sequence = 0u;
    u32 m_commits = 0u;
    u32 m_underlying = 0u;
    b8 m_focused = false;
    b8 m_visible = true;
    b8 m_enabled = true;
    b8 m_bottom = false;
    b8 m_displayChanged = false;
};

using SharedUiComboSmokeScene = RefCountPtr< RefCounter<UiComboSmokeScene>, ArenaRefDeleter<RefCounter<UiComboSmokeScene>, Core::Alloc::GlobalArena> >;

[[nodiscard]] b8 IsUiLayerComboSmokeEnabled();
[[nodiscard]] b8 IsUiLayerComboSkinSmokeEnabled();
[[nodiscard]] SharedUiComboSmokeScene CreateUiComboSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

