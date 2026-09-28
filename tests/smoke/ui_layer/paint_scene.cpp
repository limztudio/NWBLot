// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "paint_scene.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void PaintUiLayerSmokeScene(Impl::UiPaintContext& context){
    Impl::Ui::PaintBuilder& paint = context.paint;
    const f32 width = context.display.logicalWidth;
    const f32 height = context.display.logicalHeight;
    const auto rectangle = [width, height](const f32 x, const f32 y, const f32 w, const f32 h){
        return Impl::Ui::Rect{ x * width, y * height, w * width, h * height };
    };

    // Empty margins make the layer clear and complete fullscreen presentation observable in the final framebuffer.
    paint.fillRect(rectangle(0.04f, 0.08f, 0.28f, 0.20f), { 0.0f, 0.0f, 1.0f, 1.0f });
    paint.fillRect(rectangle(0.12f, 0.12f, 0.20f, 0.12f), { 1.0f, 0.0f, 0.0f, 0.5f });
    paint.fillRect(rectangle(0.34f, 0.08f, 0.16f, 0.20f), { 1.0f, 0.0f, 0.0f, 0.5f });
    paint.fillRect(rectangle(0.54f, 0.08f, 0.18f, 0.20f), { 0.25f, 0.25f, 0.25f, 1.0f });
    if(!paint.drawRegion(Name("white"), rectangle(0.76f, 0.08f, 0.20f, 0.20f), { 0.1f, 0.8f, 0.2f, 0.5f })){
        NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: white sprite is absent from the default atlas"));
        return;
    }

    paint.fillRect(rectangle(0.04f, 0.38f, 0.28f, 0.26f), { 0.0f, 0.2f, 0.3f, 1.0f });
    paint.pushClip(rectangle(0.10f, 0.44f, 0.16f, 0.12f));
    paint.fillRect(rectangle(0.04f, 0.38f, 0.28f, 0.26f), { 0.0f, 1.0f, 0.0f, 1.0f });
    paint.pushClip(rectangle(0.14f, 0.42f, 0.16f, 0.16f));
    paint.fillRect(rectangle(0.04f, 0.38f, 0.28f, 0.26f), { 1.0f, 0.0f, 0.0f, 1.0f });
    if(!paint.popClip() || !paint.popClip()){
        NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: nested clip stack became unbalanced"));
        return;
    }

    if(
        !paint.drawRegion(Name("panel.normal"), rectangle(0.36f, 0.40f, 0.26f, 0.24f))
        || !paint.drawRegion(Name("button.normal"), rectangle(0.66f, 0.40f, 0.13f, 0.14f))
        || !paint.drawRegion(Name("button.hover"), rectangle(0.82f, 0.40f, 0.14f, 0.14f))
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: named nine-slice regions are absent from the default atlas"));
        return;
    }

    paint.fillRect(rectangle(0.64f, 0.64f, 0.16f, 0.16f), { 0.0f, 0.1f, 0.2f, 1.0f });
    if(!paint.drawRegion(Name("combo.arrow"), rectangle(0.68f, 0.68f, 0.08f, 0.08f)))
        NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: combo arrow sprite is absent from the default atlas"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

