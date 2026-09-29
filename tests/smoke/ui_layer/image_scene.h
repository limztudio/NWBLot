// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "image_snapshot.h"

#include <impl/ecs_ui/components.h>

#include <core/input/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// RefCounter derives from this scene; image declarations retain copied region, size and tint inputs.
class UiImageSmokeScene : public Core::IInputEventHandler, NoCopy{
public:
    UiImageSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input);
    virtual ~UiImageSmokeScene()override;


public:
    [[nodiscard]] bool paint(Impl::UiPaintContext& context);
    virtual bool keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods)override;


private:
    [[nodiscard]] bool paintMain(Impl::Ui::Builder& ui, const Impl::Ui::Rect& bounds);
    [[nodiscard]] bool paintFrozen(Impl::Ui::Builder& ui, const Impl::Ui::Rect& bounds);
    [[nodiscard]] bool paintExternal(Impl::UiPaintContext& context, const Impl::Ui::Rect& bounds);
    [[nodiscard]] bool paintPopups(Impl::Ui::Builder& ui, f32 right);
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
    bool m_phase = false;
};

using SharedUiImageSmokeScene = RefCountPtr<
    RefCounter<UiImageSmokeScene>, ArenaRefDeleter<RefCounter<UiImageSmokeScene>, Core::Alloc::GlobalArena>
>;

[[nodiscard]] bool IsUiLayerImageSmokeEnabled();
[[nodiscard]] bool IsUiLayerImageSkinSmokeEnabled();
[[nodiscard]] SharedUiImageSmokeScene CreateUiImageSmokeScene(
    Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

