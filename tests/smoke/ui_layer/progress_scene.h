// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "progress_snapshot.h"

#include <impl/ecs_ui/paint_context.h>

#include <core/input/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Scene fixture for passive progress bar declarations.
class UiProgressSmokeScene : public Core::IInputEventHandler, NoCopy{
public:
    explicit UiProgressSmokeScene(Core::InputDispatcher& input);
    virtual ~UiProgressSmokeScene()noexcept override;


public:
    [[nodiscard]] b8 paint(Impl::UiPaintContext& context);
    virtual b8 keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods)noexcept override;


private:
    [[nodiscard]] b8 paintMain(Impl::Ui::Builder& ui, const Impl::Ui::Rect& bounds);
    [[nodiscard]] b8 paintFrozen(Impl::Ui::Builder& ui, const Impl::Ui::Rect& bounds);
    [[nodiscard]] b8 paintExternal(Impl::UiPaintContext& context, const Impl::Ui::Rect& bounds);
    [[nodiscard]] b8 paintPopups(Impl::Ui::Builder& ui, f32 right);
    void observeState(Impl::UiPaintContext& context);
    void paintMarkers(Impl::UiPaintContext& context)const;


private:
    Core::InputDispatcher& m_input;
    Impl::Ui::PopupState m_parent;
    Impl::Ui::PopupState m_child;
    UiProgressSnapshot m_declared;
    UiProgressSnapshot m_snapshot;
    u32 m_beforeClicks = 0u;
    u32 m_afterClicks = 0u;
    b8 m_phase = false;
};

using SharedUiProgressSmokeScene = RefCountPtr< RefCounter<UiProgressSmokeScene>, ArenaRefDeleter<RefCounter<UiProgressSmokeScene>, Core::Alloc::GlobalArena> >;

[[nodiscard]] b8 IsUiLayerProgressSmokeEnabled();
[[nodiscard]] b8 IsUiLayerProgressSkinSmokeEnabled();
[[nodiscard]] SharedUiProgressSmokeScene CreateUiProgressSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

