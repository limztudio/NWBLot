// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "texture_image_snapshot.h"

#include <impl/ecs_ui/paint_context.h>
#include <impl/ecs_ui/toolkit/images/image_loader.h>

#include <core/input/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Scene fixture for primitive and Builder texture image declarations.
class UiTextureImageSmokeScene : public Core::IInputEventHandler, NoCopy{
public:
    UiTextureImageSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input, Core::Assets::AssetManager& assets);
    virtual ~UiTextureImageSmokeScene()noexcept override;


public:
    [[nodiscard]] b8 paint(Impl::UiPaintContext& context);
    [[nodiscard]] virtual b8 keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods)override;


private:
    [[nodiscard]] b8 replaceVersion();
    [[nodiscard]] b8 paintControls(Impl::Ui::Builder& ui, const Impl::Ui::Rect& bounds);
    [[nodiscard]] b8 paintImages(Impl::UiPaintContext& context, f32 width, f32 right, f32 otherWidth);
    [[nodiscard]] b8 paintBuilderImages(Impl::Ui::Builder& ui, const Impl::Ui::Rect& bounds);
    [[nodiscard]] b8 paintPopups(Impl::UiPaintContext& context, f32 right);
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
    b8 m_phase = false;
    b8 m_replacementAlternate = false;
    b8 m_sourcesValid = false;
};

using SharedUiTextureImageSmokeScene = RefCountPtr< RefCounter<UiTextureImageSmokeScene>, ArenaRefDeleter<RefCounter<UiTextureImageSmokeScene>, Core::Alloc::GlobalArena> >;

[[nodiscard]] b8 IsUiLayerTextureImageSmokeEnabled();
[[nodiscard]] b8 IsUiLayerTextureImageSkinSmokeEnabled();
[[nodiscard]] SharedUiTextureImageSmokeScene CreateUiTextureImageSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input, Core::Assets::AssetManager& assets);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

