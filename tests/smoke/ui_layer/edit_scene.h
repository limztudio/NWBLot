// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/paint_context.h>
#include <impl/ecs_ui/toolkit/widgets/edit_box_state.h>

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
    virtual ~UiEditSmokeScene()noexcept override;


public:
    [[nodiscard]] b8 paint(Impl::UiPaintContext& context);
    virtual b8 keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods)override;


private:
    void resetModels();
    [[nodiscard]] Array<u32, 12u> values()const noexcept;
    void observeState(Impl::Ui::TextService& text);


private:
    Core::Alloc::GlobalArena& m_arena;
    Core::InputDispatcher& m_input;
    Impl::Ui::EditModel m_primary;
    Impl::Ui::EditModel m_secondary;
    Impl::Ui::EditBoxState m_primaryState;
    Impl::Ui::EditBoxState m_secondaryState;
    Impl::Ui::EditBoxPlacement m_lastPrimary;
    Impl::Ui::EditBoxPlacement m_lastSecondary;
    Impl::Ui::Rect m_lastPrimarySelection;
    Impl::Ui::Rect m_lastSecondarySelection;
    Impl::Ui::DisplayMetrics m_lastDisplay;
    Array<u32, 12u> m_lastValues{};
    u32 m_sequence = 0u;
    b8 m_primaryFocused = false;
    b8 m_secondaryFocused = false;
    b8 m_readOnly = false;
    b8 m_visible = true;
    b8 m_displayChanged = false;
};

using SharedUiEditSmokeScene = RefCountPtr< RefCounter<UiEditSmokeScene>, ArenaRefDeleter<RefCounter<UiEditSmokeScene>, Core::Alloc::GlobalArena> >;

[[nodiscard]] b8 IsUiLayerEditSmokeEnabled();
[[nodiscard]] SharedUiEditSmokeScene CreateUiEditSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

