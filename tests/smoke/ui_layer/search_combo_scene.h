// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "search_combo_source.h"

#include <impl/ecs_ui/components.h>
#include <impl/ecs_ui/toolkit/widgets/search_combo.h>

#include <core/alloc/general.h>
#include <core/input/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiSearchComboSmokeScene : public Core::IInputEventHandler, NoCopy{
public:
    UiSearchComboSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input);
    virtual ~UiSearchComboSmokeScene()override;


public:
    [[nodiscard]] bool paint(Impl::UiPaintContext& context);
    virtual bool keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods)override;


private:
    void observeDisplay(const Impl::Ui::DisplayMetrics& display);
    void observeState(Impl::Ui::TextService& text);
    void paintMarkers(Impl::UiPaintContext& context)const;
    [[nodiscard]] Array<u64, 22u> values()const;
    [[nodiscard]] Impl::Ui::Rect cursorRow()const;


private:
    Core::Alloc::GlobalArena& m_arena;
    Core::InputDispatcher& m_input;
    UiSearchComboSmokeSource m_source;
    Impl::Ui::SearchComboState m_state;
    Impl::Ui::ScrollPlacement m_lastPlacement;
    Impl::Ui::PopupPlacement m_lastPopup;
    Impl::Ui::Rect m_lastBounds;
    Impl::Ui::Rect m_lastQueryBounds;
    Impl::Ui::DisplayMetrics m_lastDisplay;
    Array<u64, 22u> m_lastValues{};
    u32 m_sequence = 0u;
    u32 m_commits = 0u;
    u32 m_underlying = 0u;
    bool m_focused = false;
    bool m_enabled = true;
    bool m_displayChanged = false;
};

using SharedUiSearchComboSmokeScene = RefCountPtr<
    RefCounter<UiSearchComboSmokeScene>, ArenaRefDeleter<RefCounter<UiSearchComboSmokeScene>, Core::Alloc::GlobalArena>
>;

[[nodiscard]] bool IsUiLayerSearchComboSmokeEnabled();
[[nodiscard]] bool IsUiLayerSearchComboSkinSmokeEnabled();
[[nodiscard]] SharedUiSearchComboSmokeScene CreateUiSearchComboSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

