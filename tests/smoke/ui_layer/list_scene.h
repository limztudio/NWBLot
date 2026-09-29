// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "list_source.h"

#include <impl/ecs_ui/components.h>

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
    virtual ~UiListSmokeScene()override;


public:
    [[nodiscard]] bool paint(Impl::UiPaintContext& context);
    virtual bool keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods)override;


private:
    void observeDisplay(const Impl::Ui::DisplayMetrics& display);
    void observeState();
    void paintMarkers(Impl::UiPaintContext& context)const;
    [[nodiscard]] Array<u64, 12u> values()const;
    [[nodiscard]] Impl::Ui::Rect selectedRow()const;


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
    bool m_focused = false;
    bool m_visible = true;
    bool m_displayChanged = false;
};

using SharedUiListSmokeScene = RefCountPtr<
    RefCounter<UiListSmokeScene>, ArenaRefDeleter<RefCounter<UiListSmokeScene>, Core::Alloc::GlobalArena>
>;

[[nodiscard]] bool IsUiLayerListSmokeEnabled();
[[nodiscard]] bool IsUiLayerListSkinSmokeEnabled();
[[nodiscard]] SharedUiListSmokeScene CreateUiListSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

