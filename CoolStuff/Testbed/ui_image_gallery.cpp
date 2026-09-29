// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "ui_image_gallery.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void TestbedUiImageGallery::paint(NWB::Impl::UiPaintContext& context, const f32 x, const f32 y){
    using namespace NWB::Impl::Ui;
    Builder& ui = context.ui;
    if(!ui.beginPanel("image_gallery", { x, y, 280.0f, 180.0f }))
        return;
    bool valid = ui.label("title", "Atlas images");
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
    valid = ui.label("hint", "Natural size, sprite and nine-slice") && valid;
    valid = ui.endPanel() && valid;
    if(!valid)
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom image declaration failed"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

