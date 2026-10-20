// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "ui_image_gallery.h"
#include "ui_gallery_constants.h"

#include <impl/ecs_ui/toolkit/images/image_loader.h>

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiImageGallery::UiImageGallery(NWB::Core::Alloc::GlobalArena& arena, const NWB::Core::Assets::AssetManager& assets){
    auto source = NWB::Impl::Ui::LoadImageSource(arena, assets, NWB::Core::Assets::AssetRef<NWB::Impl::Texture>{"engine/ui/skins/default/texture"});
    if(source)
        m_source = Move(*source);
}

void UiImageGallery::paint(NWB::Impl::UiPaintContext& context, const f32 x, const f32 y){
    using namespace NWB::Impl::Ui;
    constexpr f32 s_PanelHeight = 220.0f;
    constexpr f32 s_SpriteSize = 64.0f;
    constexpr f32 s_SliceWidth = 96.0f;
    constexpr Color s_SliceTint{ 0.8f, 0.9f, 1.0f, 0.8f };
    constexpr f32 s_TextureHeight = 48.0f;
    Builder& ui = context.ui;
    if(!ui.beginPanel("image_gallery", { x, y, s_OrdinaryGalleryWidth, s_PanelHeight }))
        return;
    bool valid = ui.label(s_TitleKey, "Atlas and engine images");
    valid = ui.beginRow("samples") && valid;
    ImageOptions sprite;
    sprite.width = { LayoutSizePolicy::Fixed, s_SpriteSize };
    sprite.height = { LayoutSizePolicy::Fixed, s_SpriteSize };
    valid = ui.image("sprite", Name("radio.mark"), sprite) && valid;
    ImageOptions slice = sprite;
    slice.width.value = s_SliceWidth;
    slice.tint = s_SliceTint;
    valid = ui.image("slice", s_ButtonNormal, slice) && valid;
    valid = ui.image("natural", s_ComboArrow) && valid;
    valid = ui.endContainer() && valid;
    ImageOptions texture;
    texture.width = { LayoutSizePolicy::Stretch, 1.0f };
    texture.height = { LayoutSizePolicy::Fixed, s_TextureHeight };
    valid = ui.image("engine_texture", m_source, texture) && valid;
    valid = ui.label(s_HintKey,"Texture, sprite and nine-slice") && valid;
    valid = ui.endPanel() && valid;
    if(!valid)
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom image declaration failed"));
}


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

