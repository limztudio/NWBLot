// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "project.h"

#include <core/common/log.h>

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_skin_preview{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using PaintBuilder = NWB::Impl::Ui::PaintBuilder;

static constexpr f32 s_PanelWidth = 280.0f;
static constexpr f32 s_PanelHeight = 216.0f;
static constexpr f32 s_Margin = 18.0f;
static const Name s_Panel("panel.normal");
static const Name s_ButtonNormal("button.normal");
static const Name s_ButtonHover("button.hover");
static const Name s_ButtonPressed("button.pressed");
static const Name s_ButtonDisabled("button.disabled");
static const Name s_EditFocused("edit.focused");
static const Name s_ComboArrow("combo.arrow");
static const Name s_Checkbox("checkbox.checked");
static const Name s_CheckboxMark("checkbox.mark");
static const Name s_ProgressTrack("progress.track");
static const Name s_ProgressFill("progress.fill");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool DrawSkinGallery(PaintBuilder& paint, const f32 x, const f32 y){
    return
        paint.drawRegion(s_Panel, { x, y, s_PanelWidth, s_PanelHeight })
        && paint.drawRegion(s_ButtonNormal, { x + 12.0f, y + 12.0f, 122.0f, 30.0f })
        && paint.drawRegion(s_ButtonHover, { x + 146.0f, y + 12.0f, 122.0f, 30.0f })
        && paint.drawRegion(s_ButtonPressed, { x + 12.0f, y + 50.0f, 122.0f, 30.0f })
        && paint.drawRegion(s_ButtonDisabled, { x + 146.0f, y + 50.0f, 122.0f, 30.0f })
        && paint.drawRegion(s_EditFocused, { x + 12.0f, y + 88.0f, 218.0f, 32.0f })
        && paint.drawRegion(s_ComboArrow, { x + 204.0f, y + 96.0f, 16.0f, 16.0f })
        && paint.drawRegion(s_Checkbox, { x + 242.0f, y + 90.0f, 26.0f, 26.0f })
        && paint.drawRegion(s_CheckboxMark, { x + 247.0f, y + 95.0f, 16.0f, 16.0f })
        && paint.drawRegion(s_ProgressTrack, { x + 12.0f, y + 132.0f, 256.0f, 16.0f })
        && paint.drawRegion(s_ProgressFill, { x + 14.0f, y + 134.0f, 168.0f, 12.0f })
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Geometry gallery exercises atlas borders, ordering, clipping and linear transparency before widgets/text land.
void ProjectTestbed::drawCustomUiControls(NWB::Impl::UiPaintContext& context){
    const f32 x = Max(__hidden_ui_skin_preview::s_Margin, context.display.logicalWidth - __hidden_ui_skin_preview::s_PanelWidth - __hidden_ui_skin_preview::s_Margin);
    const f32 y = __hidden_ui_skin_preview::s_Margin;
    if(!__hidden_ui_skin_preview::DrawSkinGallery(context.paint, x, y)){
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: default UI skin is missing a gallery region"));
        return;
    }

    context.paint.pushClip({ x + 12.0f, y + 160.0f, 256.0f, 42.0f });
    context.paint.fillRect({ x + 2.0f, y + 160.0f, 184.0f, 34.0f }, { 0.12f, 0.48f, 1.0f, 0.65f });
    context.paint.fillRect({ x + 100.0f, y + 176.0f, 184.0f, 34.0f }, { 1.0f, 0.18f, 0.08f, 0.6f });
    const bool clipRestored = context.paint.popClip();
    NWB_FATAL_ASSERT(clipRestored);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

