// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "image_snapshot.h"

#include <impl/ecs_ui/paint_context.h>

#include <core/input/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Scene fixture for passive image region, size, and tint declarations.
class UiImageSmokeScene : public Core::IInputEventHandler, NoCopy{
public:
    explicit UiImageSmokeScene(Core::InputDispatcher& input);
    virtual ~UiImageSmokeScene()noexcept override;


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
    UiImageSnapshot m_declared;
    UiImageSnapshot m_snapshot;
    u32 m_beforeClicks = 0u;
    u32 m_afterClicks = 0u;
    b8 m_phase = false;
};

using SharedUiImageSmokeScene = RefCountPtr< RefCounter<UiImageSmokeScene>, ArenaRefDeleter<RefCounter<UiImageSmokeScene>, Core::Alloc::GlobalArena> >;

[[nodiscard]] b8 IsUiLayerImageSmokeEnabled();
[[nodiscard]] b8 IsUiLayerImageSkinSmokeEnabled();
[[nodiscard]] SharedUiImageSmokeScene CreateUiImageSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

