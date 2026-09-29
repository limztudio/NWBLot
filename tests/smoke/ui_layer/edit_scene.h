// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/components.h>
#include <impl/ui/widgets/edit_box_state.h>

#include <core/alloc/general.h>
#include <core/input/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiEditSmokeScene : public Core::IInputEventHandler, NoCopy{
public:
    UiEditSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input);
    virtual ~UiEditSmokeScene()override;


public:
    [[nodiscard]] bool paint(Impl::UiPaintContext& context);
    virtual bool keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods)override;


private:
    void resetModels();
    void observeDisplay(const Impl::Ui::DisplayMetrics& display);
    void observeState();
    void paintMarkers(Impl::UiPaintContext& context)const;
    [[nodiscard]] Array<u32, 12u> values()const;


private:
    Core::InputDispatcher& m_input;
    Impl::Ui::EditModel m_primary;
    Impl::Ui::EditModel m_secondary;
    Impl::Ui::EditBoxState m_primaryState;
    Impl::Ui::EditBoxState m_secondaryState;
    Impl::Ui::EditBoxPlacement m_lastPrimary;
    Impl::Ui::EditBoxPlacement m_lastSecondary;
    Impl::Ui::DisplayMetrics m_lastDisplay;
    Array<u32, 12u> m_lastValues{};
    u32 m_sequence = 0u;
    bool m_primaryFocused = false;
    bool m_secondaryFocused = false;
    bool m_readOnly = false;
    bool m_visible = true;
    bool m_displayChanged = false;
};

using SharedUiEditSmokeScene = RefCountPtr<
    RefCounter<UiEditSmokeScene>, ArenaRefDeleter<RefCounter<UiEditSmokeScene>, Core::Alloc::GlobalArena>
>;

[[nodiscard]] bool IsUiLayerEditSmokeEnabled();
[[nodiscard]] SharedUiEditSmokeScene CreateUiEditSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

