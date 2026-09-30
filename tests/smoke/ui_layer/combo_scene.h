// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "combo_source.h"

#include <impl/ecs_ui/components.h>
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
    virtual ~UiComboSmokeScene()override;


public:
    [[nodiscard]] bool paint(Impl::UiPaintContext& context);
    virtual bool keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods)override;


private:
    void observeDisplay(const Impl::Ui::DisplayMetrics& display);
    void observeState();
    void paintMarkers(Impl::UiPaintContext& context)const;
    [[nodiscard]] Array<u64, 16u> values()const;
    [[nodiscard]] Impl::Ui::Rect cursorRow()const;


private:
    Core::InputDispatcher& m_input;
    UiComboSmokeSource m_source;
    Impl::Ui::ComboState m_state;
    Impl::Ui::ScrollPlacement m_lastPlacement;
    Impl::Ui::PopupPlacement m_lastPopup;
    Impl::Ui::Rect m_lastBounds;
    Impl::Ui::DisplayMetrics m_lastDisplay;
    Array<u64, 16u> m_lastValues{};
    u32 m_sequence = 0u;
    u32 m_commits = 0u;
    u32 m_underlying = 0u;
    bool m_focused = false;
    bool m_visible = true;
    bool m_enabled = true;
    bool m_bottom = false;
    bool m_displayChanged = false;
};

using SharedUiComboSmokeScene = RefCountPtr<
    RefCounter<UiComboSmokeScene>, ArenaRefDeleter<RefCounter<UiComboSmokeScene>, Core::Alloc::GlobalArena>
>;

[[nodiscard]] bool IsUiLayerComboSmokeEnabled();
[[nodiscard]] bool IsUiLayerComboSkinSmokeEnabled();
[[nodiscard]] SharedUiComboSmokeScene CreateUiComboSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

