// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "list_source.h"

#include <impl/ecs_ui/paint_context.h>

#include <core/alloc/general.h>
#include <core/input/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiListSmokeScene : public Core::IInputEventHandler, NoCopy{
public:
    explicit UiListSmokeScene(Core::InputDispatcher& input);
    virtual ~UiListSmokeScene()noexcept override;


public:
    [[nodiscard]] b8 paint(Impl::UiPaintContext& context);
    virtual b8 keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods)noexcept override;


private:
    [[nodiscard]] Array<u64, 12u> values()const noexcept;
    [[nodiscard]] Impl::Ui::Rect selectedRow()const noexcept;
    void observeState();


private:
    Core::InputDispatcher& m_input;
    UiListSmokeSource m_source;
    Impl::Ui::ListState m_state;
    Impl::Ui::ScrollPlacement m_lastPlacement;
    Impl::Ui::DisplayMetrics m_lastDisplay;
    Array<u64, 12u> m_lastValues{};
    u32 m_sequence = 0u;
    u32 m_commits = 0u;
    u32 m_underlying = 0u;
    b8 m_focused = false;
    b8 m_visible = true;
    b8 m_displayChanged = false;
};

using SharedUiListSmokeScene = RefCountPtr< RefCounter<UiListSmokeScene>, ArenaRefDeleter<RefCounter<UiListSmokeScene>, Core::Alloc::GlobalArena> >;

[[nodiscard]] b8 IsUiLayerListSmokeEnabled();
[[nodiscard]] b8 IsUiLayerListSkinSmokeEnabled();
[[nodiscard]] SharedUiListSmokeScene CreateUiListSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

