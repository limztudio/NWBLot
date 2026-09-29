// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "texture_image_snapshot.h"

#include <impl/ecs_ui/components.h>
#include <impl/ui/images/image_loader.h>

#include <core/graphics/runtime/runtime.h>
#include <core/input/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// RefCounter owns the scene; direct paint declarations copy immutable source versions and geometry.
class UiTextureImageSmokeScene : public Core::IInputEventHandler, NoCopy{
public:
    UiTextureImageSmokeScene(
        Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input, const Core::Assets::AssetManager& assets);
    virtual ~UiTextureImageSmokeScene()override;


public:
    [[nodiscard]] bool paint(Impl::UiPaintContext& context);
    [[nodiscard]] virtual bool keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods)override;


private:
    [[nodiscard]] bool replaceVersion();
    [[nodiscard]] bool paintControls(Impl::Ui::Builder& ui, const Impl::Ui::Rect& bounds);
    [[nodiscard]] bool paintImages(Impl::UiPaintContext& context, f32 width, f32 right, f32 otherWidth);
    [[nodiscard]] bool paintPopups(Impl::UiPaintContext& context, f32 right);
    [[nodiscard]] bool paintPopupImage(Impl::UiPaintContext& context, bool child);
    void observeState(Impl::UiPaintContext& context);
    void paintMarkers(Impl::UiPaintContext& context)const;


private:
    Core::Alloc::GlobalArena& m_arena;
    Core::InputDispatcher& m_input;
    Impl::Ui::SharedImageSource m_default;
    Impl::Ui::SharedImageSource m_alternate;
    Impl::Ui::SharedImageSource m_replacement;
    Impl::Ui::PopupState m_parent;
    Impl::Ui::PopupState m_child;
    UiTextureImageSnapshot m_declared;
    UiTextureImageSnapshot m_snapshot;
    u32 m_beforeClicks = 0u;
    u32 m_afterClicks = 0u;
    u32 m_replacementCount = 0u;
    u32 m_evictionRemaining = 0u;
    u32 m_evictionCompleted = 0u;
    bool m_phase = false;
    bool m_replacementAlternate = false;
    bool m_sourcesValid = false;
};

using SharedUiTextureImageSmokeScene = RefCountPtr<
    RefCounter<UiTextureImageSmokeScene>,
    ArenaRefDeleter<RefCounter<UiTextureImageSmokeScene>, Core::Alloc::GlobalArena>
>;

[[nodiscard]] bool IsUiLayerTextureImageSmokeEnabled();
[[nodiscard]] bool IsUiLayerTextureImageSkinSmokeEnabled();
[[nodiscard]] SharedUiTextureImageSmokeScene CreateUiTextureImageSmokeScene(
    Core::Alloc::GlobalArena& arena,
    Core::InputDispatcher& input,
    const Core::Assets::AssetManager& assets,
    Core::GraphicsRuntime& graphics
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

