// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "slider_snapshot.h"

#include <impl/ecs_ui/paint_context.h>

#include <core/input/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Scene fixture owning slider values across enclosing Builder scopes.
class UiSliderSmokeScene : public Core::IInputEventHandler, NoCopy{
public:
    explicit UiSliderSmokeScene(Core::InputDispatcher& input);
    virtual ~UiSliderSmokeScene()noexcept override;


public:
    [[nodiscard]] b8 paint(Impl::UiPaintContext& context);
    virtual b8 keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods)noexcept override;


private:
    [[nodiscard]] b8 paintMain(Impl::Ui::Builder& ui, const Impl::Ui::Rect& bounds);
    [[nodiscard]] b8 paintOther(Impl::Ui::Builder& ui, const Impl::Ui::Rect& bounds);
    [[nodiscard]] b8 paintPopup(Impl::Ui::Builder& ui, f32 right);
    void observeState(Impl::UiPaintContext& context);
    void paintMarkers(Impl::UiPaintContext& context)const;


private:
    Core::InputDispatcher& m_input;
    Impl::Ui::SliderState m_state;
    Impl::Ui::SliderState m_disabled;
    Impl::Ui::SliderState m_constant;
    Impl::Ui::SliderState m_popupSlider;
    Impl::Ui::PopupState m_parent;
    UiSliderSnapshot m_snapshot;
    u32 m_changes = 0u;
    u32 m_popupChanges = 0u;
    u32 m_beforeClicks = 0u;
    u32 m_afterClicks = 0u;
    u32 m_externalIntents = 0u;
    b8 m_enabled = true;
    b8 m_narrowRange = false;
    b8 m_coarseStep = false;
};

using SharedUiSliderSmokeScene = RefCountPtr< RefCounter<UiSliderSmokeScene>, ArenaRefDeleter<RefCounter<UiSliderSmokeScene>, Core::Alloc::GlobalArena> >;

[[nodiscard]] b8 IsUiLayerSliderSmokeEnabled();
[[nodiscard]] b8 IsUiLayerSliderSkinSmokeEnabled();
[[nodiscard]] SharedUiSliderSmokeScene CreateUiSliderSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

