// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "ui_image_gallery.h"
#include "CoolStuff/Testbed/ui_gallery_constants.adhd.h"

#include <impl/assets_ui_skin/asset.h>
#include <impl/assets_ui_skin/types.adhd.h>
#include <impl/ecs_ui/toolkit/images/image_loader.h>

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 UiImageGallery::loadAssets(NWB::Core::Alloc::GlobalArena& arena, const NWB::Core::Assets::AssetManager& assets, const NWB::Impl::ActiveSceneSubsystems& sceneSubsystems){
    const NWB::Impl::UiSubsystem* const uiSubsystem = sceneSubsystems.findSubsystem<NWB::Impl::UiSubsystem>();
    if(!uiSubsystem || !uiSubsystem->basicSkin.valid())
        return false;
    auto skin = assets.loadTypedSync<NWB::Impl::UiSkin>(uiSubsystem->basicSkin.name(), NWB_TEXT("UiImageGallery"));
    if(!skin)
        return false;
    auto source = NWB::Impl::Ui::LoadImageSource(arena, assets, (*skin)->texture());
    if(!source)
        return false;
    m_source = Move(*source);
    return true;
}

void UiImageGallery::paint(NWB::Impl::UiPaintContext& context, const f32 x, const f32 y){
    using namespace NWB::Impl::Ui;
    Builder& ui = context.ui;
    if(!ui.beginPanel("image_gallery", { x, y, s_OrdinaryGalleryWidth, s_ImageGalleryPanelHeight }))
        return;
    b8 valid = ui.label(s_TitleKey, "Atlas and engine images");
    valid = ui.beginRow("samples") && valid;
    ImageOptions sprite;
    sprite.width = { LayoutSizePolicy::Fixed, s_ImageGallerySpriteSize };
    sprite.height = { LayoutSizePolicy::Fixed, s_ImageGallerySpriteSize };
    valid = ui.image("sprite", s_RadioMarkRegion, sprite) && valid;
    ImageOptions slice = sprite;
    slice.width.value = s_ImageGallerySliceWidth;
    slice.tint = ResolveImageGallerySliceTint();
    valid = ui.image("slice", s_ButtonNormal, slice) && valid;
    valid = ui.image("natural", s_ComboArrow) && valid;
    valid = ui.endContainer() && valid;
    ImageOptions texture;
    texture.width = { LayoutSizePolicy::Stretch, 1.0f };
    texture.height = { LayoutSizePolicy::Fixed, s_ImageGalleryTextureHeight };
    valid = ui.image("engine_texture", m_source, texture) && valid;
    valid = ui.label(s_HintKey,"Texture, sprite and nine-slice") && valid;
    valid = ui.endPanel() && valid;
    if(!valid)
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom image declaration failed"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

