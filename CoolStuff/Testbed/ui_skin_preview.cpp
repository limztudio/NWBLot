// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "ui_skin_preview.h"
#include "project.h"

#include <core/common/log.h>

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_skin_preview{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using PaintBuilder = NWB::Impl::Ui::PaintBuilder;
using Label = NWB::Impl::Ui::Label;
using ShapeRequest = NWB::Impl::Ui::ShapeRequest;

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
static constexpr StringView s_KoreanText = "\xED\x95\x9C\xEA\xB8\x80 \xEC\xA1\xB0\xED\x95\xA9";


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

[[nodiscard]] static bool ConfigureLabel(Label& label, const StringView text, const f32 size = 13.0f){
    return label.setText(ShapeRequest{ .text = text, .fontSize = size }) == NWB::Impl::Ui::TextLayoutStatus::Success;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TestbedUiSkinPreview::TestbedUiSkinPreview(
    NWB::Core::Alloc::GlobalArena& arena, const NWB::Core::Assets::AssetManager& assets)
    : m_normal(arena)
    , m_hover(arena)
    , m_pressed(arena)
    , m_disabled(arena)
    , m_edit(arena)
    , m_caption(arena)
    , m_korean(arena)
    , m_widgets(arena, assets)
{
    using namespace __hidden_ui_skin_preview;
    const bool configured = ConfigureLabel(m_normal, "Normal") && ConfigureLabel(m_hover, "Hover")
        && ConfigureLabel(m_pressed, "Pressed") && ConfigureLabel(m_disabled, "Disabled")
        && ConfigureLabel(m_edit, "UTF-8 labels", 14.0f) && ConfigureLabel(m_caption, "FreeType + HarfBuzz", 14.0f)
        && m_korean.setText(ShapeRequest{
            .text = s_KoreanText,
            .fontSize = 13.0f,
            .scriptTag = NWB::Impl::Ui::TextScriptTag('H', 'a', 'n', 'g'),
            .language = "ko"
        }) == NWB::Impl::Ui::TextLayoutStatus::Success;
    GLB_FATAL_ASSERT_MSG(configured, GLB_TEXT("Testbed UI labels must have valid constant text"));
}

void TestbedUiSkinPreview::paint(NWB::Impl::UiPaintContext& context){
    const f32 x = Max(__hidden_ui_skin_preview::s_Margin, context.display.logicalWidth - __hidden_ui_skin_preview::s_PanelWidth - __hidden_ui_skin_preview::s_Margin);
    const f32 y = __hidden_ui_skin_preview::s_Margin;
    m_widgets.paint(context);
    const auto gallery = TestbedUiWidgetGallery::layoutBounds(context.display);
    if(gallery.y < y + __hidden_ui_skin_preview::s_PanelHeight + 12.0f || x < 390.0f)
        return;
    if(!__hidden_ui_skin_preview::DrawSkinGallery(context.paint, x, y)){
        NWB_LOGGER_ERROR(GLB_TEXT("Testbed: default UI skin is missing a gallery region"));
        return;
    }

    context.paint.pushClip({ x + 12.0f, y + 160.0f, 256.0f, 42.0f });
    context.paint.fillRect({ x + 2.0f, y + 160.0f, 184.0f, 34.0f }, { 0.12f, 0.48f, 1.0f, 0.65f });
    context.paint.fillRect({ x + 100.0f, y + 176.0f, 184.0f, 34.0f }, { 1.0f, 0.18f, 0.08f, 0.6f });
    const bool captionPainted = m_caption.paint(context.text, context.paint, { x + 18.0f, y + 160.0f })
        && m_korean.paint(context.text, context.paint, { x + 18.0f, y + 181.0f });
    const bool clipRestored = context.paint.popClip();
    GLB_FATAL_ASSERT(clipRestored);
    if(!captionPainted){
        NWB_LOGGER_ERROR(GLB_TEXT("Testbed: custom UI caption shaping or rasterization failed"));
        return;
    }
    const NWB::Impl::Ui::Color textColor{ 0.92f, 0.96f, 1.0f, 1.0f };
    if(
        !m_normal.paint(context.text, context.paint, { x + 20.0f, y + 17.0f }, textColor)
        || !m_hover.paint(context.text, context.paint, { x + 154.0f, y + 17.0f }, textColor)
        || !m_pressed.paint(context.text, context.paint, { x + 20.0f, y + 55.0f }, textColor)
        || !m_disabled.paint(context.text, context.paint, { x + 154.0f, y + 55.0f }, { 0.38f, 0.42f, 0.48f, 1.0f })
        || !m_edit.paint(context.text, context.paint, { x + 20.0f, y + 94.0f }, textColor)
    )
        NWB_LOGGER_ERROR(GLB_TEXT("Testbed: custom UI label shaping or rasterization failed"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ProjectTestbed::drawCustomUiControls(NWB::Impl::UiPaintContext& context){
    m_uiPreview.paint(context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

