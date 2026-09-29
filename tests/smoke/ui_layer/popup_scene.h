// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/components.h>
#include <impl/ui/widgets/popup.h>

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
    virtual ~UiPopupSmokeScene()override;


public:
    [[nodiscard]] bool paint(Impl::UiPaintContext& context);
    [[nodiscard]] bool paintLaterRoot(Impl::UiPaintContext& context)const;
    virtual bool keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods)override;


private:
    void observeDisplay(const Impl::Ui::DisplayMetrics& display);
    void observeState();
    void paintMarkers(Impl::UiPaintContext& context)const;
    [[nodiscard]] Array<u32, 12u> values()const;


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
    bool m_modal = false;
    bool m_checked = false;
    bool m_popupFocused = false;
    bool m_outsideFocused = false;
    bool m_edge = false;
    bool m_right = false;
    bool m_visible = true;
    bool m_displayChanged = false;
};

using SharedUiPopupSmokeScene = RefCountPtr<
    RefCounter<UiPopupSmokeScene>, ArenaRefDeleter<RefCounter<UiPopupSmokeScene>, Core::Alloc::GlobalArena>
>;

[[nodiscard]] bool IsUiLayerPopupSmokeEnabled();
[[nodiscard]] bool IsUiLayerPopupSkinSmokeEnabled();
[[nodiscard]] SharedUiPopupSmokeScene CreateUiPopupSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

