// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "ui_image_gallery.h"

#include <impl/ecs_ui/toolkit/images/image_loader.h>

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TestbedUiImageGallery::TestbedUiImageGallery(
    NWB::Core::Alloc::GlobalArena& arena, const NWB::Core::Assets::AssetManager& assets)
    : m_source(NWB::Impl::Ui::LoadImageSource(
        arena, assets, NWB::Core::Assets::AssetRef<NWB::Impl::Texture>{"engine/ui/skins/default/texture"}
    ))
{}

void TestbedUiImageGallery::paint(NWB::Impl::UiPaintContext& context, const f32 x, const f32 y){
    using namespace NWB::Impl::Ui;
    Builder& ui = context.ui;
    if(!ui.beginPanel("image_gallery", { x, y, 280.0f, 220.0f }))
        return;
    bool valid = ui.label("title", "Atlas and engine images");
    valid = ui.beginRow("samples") && valid;
    ImageOptions sprite;
    sprite.width = { LayoutSizePolicy::Fixed, 64.0f };
    sprite.height = { LayoutSizePolicy::Fixed, 64.0f };
    valid = ui.image("sprite", Name("radio.mark"), sprite) && valid;
    ImageOptions slice = sprite;
    slice.width.value = 96.0f;
    slice.tint = { 0.8f, 0.9f, 1.0f, 0.8f };
    valid = ui.image("slice", Name("button.normal"), slice) && valid;
    valid = ui.image("natural", Name("combo.arrow")) && valid;
    valid = ui.endContainer() && valid;
    ImageOptions texture;
    texture.width = { LayoutSizePolicy::Stretch, 1.0f };
    texture.height = { LayoutSizePolicy::Fixed, 48.0f };
    valid = ui.image("engine_texture", m_source, texture) && valid;
    valid = ui.label("hint", "Texture, sprite and nine-slice") && valid;
    valid = ui.endPanel() && valid;
    if(!valid)
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom image declaration failed"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

