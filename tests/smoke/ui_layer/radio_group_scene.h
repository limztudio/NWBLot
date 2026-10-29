// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "radio_group_source.h"
#include "radio_group_snapshot.h"

#include <impl/ecs_ui/paint_context.h>

#include <core/input/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Scene fixture owning radio group sources and states across Builder loans.
class UiRadioGroupSmokeScene : public Core::IInputEventHandler, NoCopy{
public:
    explicit UiRadioGroupSmokeScene(Core::InputDispatcher& input);
    virtual ~UiRadioGroupSmokeScene()noexcept override;


public:
    [[nodiscard]] b8 paint(Impl::UiPaintContext& context);
    virtual b8 keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods)noexcept override;


private:
    [[nodiscard]] b8 paintMain(Impl::Ui::Builder& ui);
    [[nodiscard]] b8 paintDisabled(Impl::Ui::Builder& ui);
    [[nodiscard]] b8 paintPopup(Impl::Ui::Builder& ui);
    void observeState(Impl::UiPaintContext& context);
    void paintMarkers(Impl::UiPaintContext& context)const;


private:
    Core::InputDispatcher& m_input;
    UiRadioGroupSmokeSource m_source;
    Impl::Ui::RadioGroupState m_state;
    Impl::Ui::RadioGroupState m_disabled;
    Impl::Ui::RadioGroupState m_popupRadio;
    Impl::Ui::PopupState m_parent;
    UiRadioGroupSnapshot m_snapshot;
    u32 m_changes = 0u;
    u32 m_activations = 0u;
    u32 m_beforeClicks = 0u;
    u32 m_afterClicks = 0u;
    u32 m_popupChanges = 0u;
    u32 m_popupActivations = 0u;
    b8 m_enabled = true;
};

using SharedUiRadioGroupSmokeScene = RefCountPtr< RefCounter<UiRadioGroupSmokeScene>, ArenaRefDeleter<RefCounter<UiRadioGroupSmokeScene>, Core::Alloc::GlobalArena> >;

[[nodiscard]] b8 IsUiLayerRadioGroupSmokeEnabled();
[[nodiscard]] b8 IsUiLayerRadioGroupSkinSmokeEnabled();
[[nodiscard]] SharedUiRadioGroupSmokeScene CreateUiRadioGroupSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

