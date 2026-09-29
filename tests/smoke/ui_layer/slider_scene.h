// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "slider_snapshot.h"

#include <impl/ecs_ui/components.h>

#include <core/input/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// RefCounter derives from this scene; each value owner outlives the final enclosing Builder scope.
class UiSliderSmokeScene : public Core::IInputEventHandler, NoCopy{
public:
    UiSliderSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input);
    virtual ~UiSliderSmokeScene()override;


public:
    [[nodiscard]] bool paint(Impl::UiPaintContext& context);
    virtual bool keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods)override;


private:
    [[nodiscard]] bool paintMain(Impl::Ui::Builder& ui, const Impl::Ui::Rect& bounds);
    [[nodiscard]] bool paintOther(Impl::Ui::Builder& ui, const Impl::Ui::Rect& bounds);
    [[nodiscard]] bool paintPopup(Impl::Ui::Builder& ui, f32 right);
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
    bool m_enabled = true;
    bool m_narrowRange = false;
    bool m_coarseStep = false;
};

using SharedUiSliderSmokeScene = RefCountPtr<
    RefCounter<UiSliderSmokeScene>, ArenaRefDeleter<RefCounter<UiSliderSmokeScene>, Core::Alloc::GlobalArena>
>;

[[nodiscard]] bool IsUiLayerSliderSmokeEnabled();
[[nodiscard]] bool IsUiLayerSliderSkinSmokeEnabled();
[[nodiscard]] SharedUiSliderSmokeScene CreateUiSliderSmokeScene(
    Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

