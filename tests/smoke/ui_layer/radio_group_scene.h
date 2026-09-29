// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "radio_group_source.h"
#include "radio_group_snapshot.h"

#include <impl/ecs_ui/components.h>

#include <core/input/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// RefCounter derives from this scene; its sources and states outlive every outer Builder loan.
class UiRadioGroupSmokeScene : public Core::IInputEventHandler, NoCopy{
public:
    UiRadioGroupSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input);
    virtual ~UiRadioGroupSmokeScene()override;


public:
    [[nodiscard]] bool paint(Impl::UiPaintContext& context);
    virtual bool keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods)override;


private:
    [[nodiscard]] bool paintMain(Impl::Ui::Builder& ui);
    [[nodiscard]] bool paintDisabled(Impl::Ui::Builder& ui);
    [[nodiscard]] bool paintPopup(Impl::Ui::Builder& ui);
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
    bool m_enabled = true;
};

using SharedUiRadioGroupSmokeScene = RefCountPtr<
    RefCounter<UiRadioGroupSmokeScene>, ArenaRefDeleter<RefCounter<UiRadioGroupSmokeScene>, Core::Alloc::GlobalArena>
>;

[[nodiscard]] bool IsUiLayerRadioGroupSmokeEnabled();
[[nodiscard]] bool IsUiLayerRadioGroupSkinSmokeEnabled();
[[nodiscard]] SharedUiRadioGroupSmokeScene CreateUiRadioGroupSmokeScene(
    Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

