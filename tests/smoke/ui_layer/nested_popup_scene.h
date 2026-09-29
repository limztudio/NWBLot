// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "nested_popup_source.h"

#include <impl/ecs_ui/components.h>
#include <impl/ui/widgets/context_menu.h>

#include <core/alloc/general.h>
#include <core/input/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Every nested state is a member and remains lent through the outermost endPopup.
class UiNestedPopupSmokeScene : public Core::IInputEventHandler, NoCopy{
public:
    UiNestedPopupSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input);
    virtual ~UiNestedPopupSmokeScene()override;


public:
    [[nodiscard]] bool paint(Impl::UiPaintContext& context);
    virtual bool keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods)override;


private:
    [[nodiscard]] bool paintRoot(Impl::Ui::Builder& ui);
    [[nodiscard]] bool paintParent(Impl::Ui::Builder& ui);
    [[nodiscard]] bool paintChild(Impl::Ui::Builder& ui);
    void observeDisplay(const Impl::Ui::DisplayMetrics& display);
    // Copies accepted observations during this callback; no target or popup-scope pointer survives publication.
    void observeState(Impl::UiPaintContext& context);
    void paintMarkers(Impl::UiPaintContext& context)const;
    [[nodiscard]] Array<u64, 29u> values()const;
    [[nodiscard]] Impl::Ui::Rect rowBounds(const Impl::Ui::ListState& state,
        const Impl::Ui::IListDataSource& source, u64 key, f32 rowHeight)const;


private:
    Core::InputDispatcher& m_input;
    UiNestedPopupSmokeSource m_source;
    Impl::Ui::PopupState m_parent;
    Impl::Ui::PopupState m_child;
    Impl::Ui::ListState m_beforeList;
    Impl::Ui::ListState m_afterList;
    Impl::Ui::ComboState m_combo;
    Impl::Ui::SearchComboState m_search;
    Impl::Ui::ContextMenuState m_menu;
    Impl::Ui::EditModel m_beforeText;
    Impl::Ui::EditModel m_afterText;
    Impl::Ui::EditModel m_childText;
    Impl::Ui::EditBoxState m_beforeEdit;
    Impl::Ui::EditBoxState m_afterEdit;
    Impl::Ui::EditBoxState m_childEdit;
    Impl::Ui::DisplayMetrics m_lastDisplay;
    Array<Impl::Ui::Rect, 26u> m_rectangles{};
    Array<Impl::Ui::Rect, 26u> m_lastRectangles{};
    Array<u64, 29u> m_lastValues{};
    u64 m_command = 0u;
    u32 m_sequence = 0u;
    u32 m_beforeClicks = 0u;
    u32 m_afterClicks = 0u;
    u32 m_childClicks = 0u;
    u32 m_outside = 0u;
    u32 m_focusScope = 0u;
    u32 m_focusCode = 0u;
    u32 m_popupTargets = 0u;
    u32 m_popupCount = 0u;
    bool m_childParentScope = false;
    bool m_childDeclared = false;
    bool m_displayChanged = false;
};

using SharedUiNestedPopupSmokeScene = RefCountPtr<
    RefCounter<UiNestedPopupSmokeScene>, ArenaRefDeleter<RefCounter<UiNestedPopupSmokeScene>, Core::Alloc::GlobalArena>
>;

[[nodiscard]] bool IsUiLayerNestedPopupSmokeEnabled();
[[nodiscard]] bool IsUiLayerNestedPopupSkinSmokeEnabled();
[[nodiscard]] SharedUiNestedPopupSmokeScene CreateUiNestedPopupSmokeScene(
    Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

