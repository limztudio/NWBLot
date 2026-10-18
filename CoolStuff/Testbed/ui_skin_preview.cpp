// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "ui_skin_preview.h"
#include "project.h"

#include <core/common/log.h>

#include <global/simd/vector_arithmetic.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_skin_preview{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using PaintBuilder = NWB::Impl::Ui::PaintBuilder;
using Label = NWB::Impl::Ui::Label;
using ShapeRequest = NWB::Impl::Ui::ShapeRequest;

static constexpr f32 s_PanelWidth = 280.0f;
static constexpr f32 s_Margin = 18.0f;
static const Name s_Panel("panel.normal");
static const Name s_ButtonNormal("button.normal");
static const Name s_ButtonHover("button.hover");
static const Name s_ButtonPressed("button.pressed");
static const Name s_ButtonDisabled("button.disabled");
static const Name s_EditNormal("edit.normal");
static const Name s_EditFocused("edit.focused");
static const Name s_ComboArrow("combo.arrow");
static const Name s_Checkbox("checkbox.checked");
static const Name s_CheckboxMark("checkbox.mark");
static const Name s_ProgressTrack("progress.track");
static const Name s_ProgressFill("progress.fill");
static constexpr StringView s_KoreanText = "\xED\x95\x9C\xEA\xB8\x80 \xEC\xA1\xB0\xED\x95\xA9";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool DrawSkinGallery(PaintBuilder& paint, const f32 x, const f32 y, const f32 captionHeight){
    const SIMDVectorSingle origin = VectorSet(x, y, 0.0f, 0.0f);
    const SIMDVectorSingle buttonNormal = VectorAdd(origin, VectorSet(12.0f, 12.0f, 0.0f, 0.0f));
    const SIMDVectorSingle buttonHover = VectorAdd(origin, VectorSet(146.0f, 12.0f, 0.0f, 0.0f));
    const SIMDVectorSingle buttonPressed = VectorAdd(origin, VectorSet(12.0f, 50.0f, 0.0f, 0.0f));
    const SIMDVectorSingle buttonDisabled = VectorAdd(origin, VectorSet(146.0f, 50.0f, 0.0f, 0.0f));
    const SIMDVectorSingle editFocused = VectorAdd(origin, VectorSet(12.0f, 88.0f, 0.0f, 0.0f));
    const SIMDVectorSingle comboArrow = VectorAdd(origin, VectorSet(204.0f, 96.0f, 0.0f, 0.0f));
    const SIMDVectorSingle checkbox = VectorAdd(origin, VectorSet(242.0f, 90.0f, 0.0f, 0.0f));
    const SIMDVectorSingle checkboxMark = VectorAdd(origin, VectorSet(247.0f, 95.0f, 0.0f, 0.0f));
    const SIMDVectorSingle progressTrack = VectorAdd(origin, VectorSet(12.0f, 132.0f, 0.0f, 0.0f));
    const SIMDVectorSingle progressFill = VectorAdd(origin, VectorSet(14.0f, 134.0f, 0.0f, 0.0f));
    const SIMDVectorSingle editNormal = VectorAdd(origin, VectorSet(12.0f, 158.0f, 0.0f, 0.0f));
    return
        paint.drawRegion(s_Panel, { x, y, s_PanelWidth, VectorGetX(VectorAdd(VectorReplicate(170.0f), VectorReplicate(captionHeight))) })
        && paint.drawRegion(s_ButtonNormal, { VectorGetX(buttonNormal), VectorGetY(buttonNormal), 122.0f, 30.0f })
        && paint.drawRegion(s_ButtonHover, { VectorGetX(buttonHover), VectorGetY(buttonHover), 122.0f, 30.0f })
        && paint.drawRegion(s_ButtonPressed, { VectorGetX(buttonPressed), VectorGetY(buttonPressed), 122.0f, 30.0f })
        && paint.drawRegion(s_ButtonDisabled, { VectorGetX(buttonDisabled), VectorGetY(buttonDisabled), 122.0f, 30.0f })
        && paint.drawRegion(s_EditFocused, { VectorGetX(editFocused), VectorGetY(editFocused), 218.0f, 32.0f })
        && paint.drawRegion(s_ComboArrow, { VectorGetX(comboArrow), VectorGetY(comboArrow), 16.0f, 16.0f })
        && paint.drawRegion(s_Checkbox, { VectorGetX(checkbox), VectorGetY(checkbox), 26.0f, 26.0f })
        && paint.drawRegion(s_CheckboxMark, { VectorGetX(checkboxMark), VectorGetY(checkboxMark), 16.0f, 16.0f })
        && paint.drawRegion(s_ProgressTrack, { VectorGetX(progressTrack), VectorGetY(progressTrack), 256.0f, 16.0f })
        && paint.drawRegion(s_ProgressFill, { VectorGetX(progressFill), VectorGetY(progressFill), 168.0f, 12.0f })
        && paint.drawRegion(s_EditNormal, { VectorGetX(editNormal), VectorGetY(editNormal), 256.0f, captionHeight })
    ;
}

[[nodiscard]] static bool ConfigureLabel(Label& label, const StringView text, const f32 size = 13.0f){
    return label.setText(ShapeRequest{ .text = text, .fontSize = size }) == NWB::Impl::Ui::TextLayoutStatus::Success;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiSkinPreview::UiSkinPreview(NWB::Core::Alloc::GlobalArena& arena, const NWB::Core::Assets::AssetManager& assets)
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
        && m_korean.setText(ShapeRequest{ .text = s_KoreanText, .fontSize = 13.0f, .scriptTag = NWB::Impl::Ui::TextScriptTag('H', 'a', 'n', 'g'), .language = "ko" }) == NWB::Impl::Ui::TextLayoutStatus::Success;
    NWB_FATAL_ASSERT_MSG(configured, NWB_TEXT("Testbed UI labels must have valid constant text"));
}

Expected<NWB::Impl::Ui::Rect> UiSkinPreview::galleryBounds(NWB::Impl::UiPaintContext& context){
    const auto caption = m_caption.measure(context.text, context.display);
    const auto korean = m_korean.measure(context.text, context.display);
    if(!caption || !korean)
        return MakeUnexpected(Failure{});
    const SIMDVectorSingle captionHeight = VectorAdd(VectorAdd(VectorReplicate(caption->height), VectorReplicate(korean->height)), VectorReplicate(8.0f));
    const f32 preferredTop = VectorGetX(VectorAdd(VectorAdd(VectorAdd(VectorReplicate(__hidden_ui_skin_preview::s_Margin), VectorReplicate(170.0f)), captionHeight), VectorReplicate(12.0f)));
    return UiWidgetGallery::LayoutBounds(context.display, preferredTop);
}

void UiSkinPreview::paint(NWB::Impl::UiPaintContext& context){
    const SIMDVectorSingle margin = VectorReplicate(__hidden_ui_skin_preview::s_Margin);
    const f32 x = VectorGetX(VectorMax(margin, VectorSubtract(VectorSubtract(VectorReplicate(context.display.logicalWidth), VectorReplicate(__hidden_ui_skin_preview::s_PanelWidth)), margin)));
    const f32 y = __hidden_ui_skin_preview::s_Margin;
    const auto caption = m_caption.measure(context.text, context.display);
    const auto korean = m_korean.measure(context.text, context.display);
    const auto normal = m_normal.measure(context.text, context.display);
    const auto hover = m_hover.measure(context.text, context.display);
    const auto pressed = m_pressed.measure(context.text, context.display);
    const auto disabled = m_disabled.measure(context.text, context.display);
    const auto edit = m_edit.measure(context.text, context.display);
    if(!caption || !korean || !normal || !hover || !pressed || !disabled || !edit){
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom UI label measurement failed"));
        return;
    }
    const SIMDVectorSingle captionHeight = VectorAdd(VectorAdd(VectorReplicate(caption->height), VectorReplicate(korean->height)), VectorReplicate(8.0f));
    const auto gallery = galleryBounds(context);
    if(!gallery){
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: UI gallery measurement failed"));
        return;
    }
    m_widgets.paint(context, *gallery);
    if(gallery->y < VectorGetX(VectorAdd(VectorAdd(VectorAdd(VectorReplicate(y), VectorReplicate(170.0f)), captionHeight), VectorReplicate(12.0f))) || x < 390.0f)
        return;
    if(!__hidden_ui_skin_preview::DrawSkinGallery(context.paint, x, y, VectorGetX(captionHeight))){
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: default UI skin is missing a gallery region"));
        return;
    }

    const SIMDVectorSingle origin = VectorSet(x, y, 0.0f, 0.0f);
    const SIMDVectorSingle captionOrigin = VectorAdd(origin, VectorSet(18.0f, 162.0f, 0.0f, 0.0f));
    const SIMDVectorSingle koreanOffset = VectorAdd(VectorSet(18.0f, 162.0f, 0.0f, 0.0f), VectorSet(0.0f, caption->height, 0.0f, 0.0f));
    const SIMDVectorSingle koreanOrigin = VectorAdd(origin, koreanOffset);
    const SIMDVectorSingle buttonTextOffsets = VectorScale(VectorSubtract(VectorReplicate(30.0f), VectorSet(normal->height, hover->height, pressed->height, disabled->height)), 0.5f);
    const SIMDVectorSingle buttonTextY = VectorAdd(VectorSet(12.0f, 12.0f, 50.0f, 50.0f), buttonTextOffsets);
    const SIMDVectorSingle normalOrigin = VectorAdd(origin, VectorSet(20.0f, VectorGetX(buttonTextY), 0.0f, 0.0f));
    const SIMDVectorSingle hoverOrigin = VectorAdd(origin, VectorSet(154.0f, VectorGetY(buttonTextY), 0.0f, 0.0f));
    const SIMDVectorSingle pressedOrigin = VectorAdd(origin, VectorSet(20.0f, VectorGetZ(buttonTextY), 0.0f, 0.0f));
    const SIMDVectorSingle disabledOrigin = VectorAdd(origin, VectorSet(154.0f, VectorGetW(buttonTextY), 0.0f, 0.0f));
    const SIMDVectorSingle editOffset = VectorAdd(VectorSet(20.0f, 88.0f, 0.0f, 0.0f), VectorScale(VectorSubtract(VectorSet(0.0f, 32.0f, 0.0f, 0.0f), VectorSet(0.0f, edit->height, 0.0f, 0.0f)), 0.5f));
    const SIMDVectorSingle editOrigin = VectorAdd(origin, editOffset);
    const bool captionPainted = m_caption.paint(context.text, context.paint, { VectorGetX(captionOrigin), VectorGetY(captionOrigin), 244.0f, caption->height })
        && m_korean.paint(context.text, context.paint, { VectorGetX(koreanOrigin), VectorGetY(koreanOrigin), 244.0f, korean->height });
    if(!captionPainted){
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom UI caption shaping or rasterization failed"));
        return;
    }
    const NWB::Impl::Ui::Color textColor{ 0.92f, 0.96f, 1.0f, 1.0f };
    if(
        !m_normal.paint(context.text, context.paint, { VectorGetX(normalOrigin), VectorGetY(normalOrigin), 106.0f, normal->height }, textColor)
        || !m_hover.paint(context.text, context.paint, { VectorGetX(hoverOrigin), VectorGetY(hoverOrigin), 106.0f, hover->height }, textColor)
        || !m_pressed.paint(context.text, context.paint, { VectorGetX(pressedOrigin), VectorGetY(pressedOrigin), 106.0f, pressed->height }, textColor)
        || !m_disabled.paint(context.text, context.paint, { VectorGetX(disabledOrigin), VectorGetY(disabledOrigin), 106.0f, disabled->height }, { 0.38f, 0.42f, 0.48f, 1.0f })
        || !m_edit.paint(context.text, context.paint, { VectorGetX(editOrigin), VectorGetY(editOrigin), 178.0f, edit->height }, textColor)
    )
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom UI label shaping or rasterization failed"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void Project::drawCustomUiControls(NWB::Impl::UiPaintContext& context){
    m_uiPreview.paint(context);
}


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

