// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "ui_skin_preview.h"
#include "project.h"
#include "ui_gallery_constants.h"

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
static constexpr f32 s_PanelBaseHeight = 170.0f;
static constexpr f32 s_PanelBottomGap = 12.0f;
static constexpr f32 s_CaptionPadding = 8.0f;
static constexpr f32 s_MinimumGalleryX = 390.0f;
static constexpr f32 s_LabelFontSize = 13.0f;
static constexpr f32 s_LargeLabelFontSize = 14.0f;
static constexpr f32 s_LeftColumnX = 12.0f;
static constexpr f32 s_RightColumnX = 146.0f;
static constexpr f32 s_ButtonTopY = 12.0f;
static constexpr f32 s_ButtonBottomY = 50.0f;
static constexpr f32 s_ButtonWidth = 122.0f;
static constexpr f32 s_ButtonHeight = 30.0f;
static constexpr f32 s_EditFocusedY = 88.0f;
static constexpr f32 s_EditFocusedWidth = 218.0f;
static constexpr f32 s_EditFocusedHeight = 32.0f;
static constexpr f32 s_ComboArrowX = 204.0f;
static constexpr f32 s_ComboArrowY = 96.0f;
static constexpr f32 s_ComboArrowSize = 16.0f;
static constexpr f32 s_CheckboxX = 242.0f;
static constexpr f32 s_CheckboxY = 90.0f;
static constexpr f32 s_CheckboxSize = 26.0f;
static constexpr f32 s_CheckboxMarkX = 247.0f;
static constexpr f32 s_CheckboxMarkY = 95.0f;
static constexpr f32 s_CheckboxMarkSize = 16.0f;
static constexpr f32 s_ProgressTrackY = 132.0f;
static constexpr f32 s_ProgressTrackWidth = 256.0f;
static constexpr f32 s_ProgressTrackHeight = 16.0f;
static constexpr f32 s_ProgressFillX = 14.0f;
static constexpr f32 s_ProgressFillY = 134.0f;
static constexpr f32 s_ProgressFillWidth = 168.0f;
static constexpr f32 s_ProgressFillHeight = 12.0f;
static constexpr f32 s_EditNormalY = 158.0f;
static constexpr f32 s_EditNormalWidth = 256.0f;
static constexpr f32 s_CaptionX = 18.0f;
static constexpr f32 s_CaptionY = 162.0f;
static constexpr f32 s_CaptionWidth = 244.0f;
static constexpr f32 s_LeftTextX = 20.0f;
static constexpr f32 s_RightTextX = 154.0f;
static constexpr f32 s_ButtonTextWidth = 106.0f;
static constexpr f32 s_EditTextWidth = 178.0f;
static constexpr NWB::Impl::Ui::Color s_LabelTextColor{ 0.92f, 0.96f, 1.0f, 1.0f };
static constexpr NWB::Impl::Ui::Color s_DisabledLabelTextColor{ 0.38f, 0.42f, 0.48f, 1.0f };
static const Name s_Panel("panel.normal");
static const Name s_ButtonHover("button.hover");
static const Name s_ButtonPressed("button.pressed");
static const Name s_ButtonDisabled("button.disabled");
static const Name s_EditNormal("edit.normal");
static const Name s_EditFocused("edit.focused");
static const Name s_Checkbox("checkbox.checked");
static const Name s_CheckboxMark("checkbox.mark");
static const Name s_ProgressTrack("progress.track");
static const Name s_ProgressFill("progress.fill");
static constexpr StringView s_KoreanText = "\xED\x95\x9C\xEA\xB8\x80 \xEC\xA1\xB0\xED\x95\xA9";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Origin lanes are x, y; caption height is splat across lanes.
[[nodiscard]] static bool DrawSkinGallery(PaintBuilder& paint, const SIMDVectorSingle origin, const SIMDVectorSingle captionHeight){
    const SIMDVectorSingle buttonNormal = VectorAdd(origin, VectorSet(s_LeftColumnX, s_ButtonTopY, 0.0f, 0.0f));
    const SIMDVectorSingle buttonHover = VectorAdd(origin, VectorSet(s_RightColumnX, s_ButtonTopY, 0.0f, 0.0f));
    const SIMDVectorSingle buttonPressed = VectorAdd(origin, VectorSet(s_LeftColumnX, s_ButtonBottomY, 0.0f, 0.0f));
    const SIMDVectorSingle buttonDisabled = VectorAdd(origin, VectorSet(s_RightColumnX, s_ButtonBottomY, 0.0f, 0.0f));
    const SIMDVectorSingle editFocused = VectorAdd(origin, VectorSet(s_LeftColumnX, s_EditFocusedY, 0.0f, 0.0f));
    const SIMDVectorSingle comboArrow = VectorAdd(origin, VectorSet(s_ComboArrowX, s_ComboArrowY, 0.0f, 0.0f));
    const SIMDVectorSingle checkbox = VectorAdd(origin, VectorSet(s_CheckboxX, s_CheckboxY, 0.0f, 0.0f));
    const SIMDVectorSingle checkboxMark = VectorAdd(origin, VectorSet(s_CheckboxMarkX, s_CheckboxMarkY, 0.0f, 0.0f));
    const SIMDVectorSingle progressTrack = VectorAdd(origin, VectorSet(s_LeftColumnX, s_ProgressTrackY, 0.0f, 0.0f));
    const SIMDVectorSingle progressFill = VectorAdd(origin, VectorSet(s_ProgressFillX, s_ProgressFillY, 0.0f, 0.0f));
    const SIMDVectorSingle editNormal = VectorAdd(origin, VectorSet(s_LeftColumnX, s_EditNormalY, 0.0f, 0.0f));
    return
        paint.drawRegion(s_Panel, { VectorGetX(origin), VectorGetY(origin), s_PanelWidth, VectorGetX(VectorAdd(VectorReplicate(s_PanelBaseHeight), captionHeight)) })
        && paint.drawRegion(s_ButtonNormal, { VectorGetX(buttonNormal), VectorGetY(buttonNormal), s_ButtonWidth, s_ButtonHeight })
        && paint.drawRegion(s_ButtonHover, { VectorGetX(buttonHover), VectorGetY(buttonHover), s_ButtonWidth, s_ButtonHeight })
        && paint.drawRegion(s_ButtonPressed, { VectorGetX(buttonPressed), VectorGetY(buttonPressed), s_ButtonWidth, s_ButtonHeight })
        && paint.drawRegion(s_ButtonDisabled, { VectorGetX(buttonDisabled), VectorGetY(buttonDisabled), s_ButtonWidth, s_ButtonHeight })
        && paint.drawRegion(s_EditFocused, { VectorGetX(editFocused), VectorGetY(editFocused), s_EditFocusedWidth, s_EditFocusedHeight })
        && paint.drawRegion(s_ComboArrow, { VectorGetX(comboArrow), VectorGetY(comboArrow), s_ComboArrowSize, s_ComboArrowSize })
        && paint.drawRegion(s_Checkbox, { VectorGetX(checkbox), VectorGetY(checkbox), s_CheckboxSize, s_CheckboxSize })
        && paint.drawRegion(s_CheckboxMark, { VectorGetX(checkboxMark), VectorGetY(checkboxMark), s_CheckboxMarkSize, s_CheckboxMarkSize })
        && paint.drawRegion(s_ProgressTrack, { VectorGetX(progressTrack), VectorGetY(progressTrack), s_ProgressTrackWidth, s_ProgressTrackHeight })
        && paint.drawRegion(s_ProgressFill, { VectorGetX(progressFill), VectorGetY(progressFill), s_ProgressFillWidth, s_ProgressFillHeight })
        && paint.drawRegion(s_EditNormal, { VectorGetX(editNormal), VectorGetY(editNormal), s_EditNormalWidth, VectorGetX(captionHeight) })
    ;
}

[[nodiscard]] static bool ConfigureLabel(Label& label, const StringView text, const f32 size = s_LabelFontSize){
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
        && ConfigureLabel(m_edit, "UTF-8 labels", s_LargeLabelFontSize) && ConfigureLabel(m_caption, "FreeType + HarfBuzz", s_LargeLabelFontSize)
        && m_korean.setText(ShapeRequest{ .text = s_KoreanText, .fontSize = s_LabelFontSize, .scriptTag = NWB::Impl::Ui::TextScriptTag('H', 'a', 'n', 'g'), .language = "ko" }) == NWB::Impl::Ui::TextLayoutStatus::Success;
    NWB_FATAL_ASSERT_MSG(configured, NWB_TEXT("Testbed UI labels must have valid constant text"));
}

Expected<NWB::Impl::Ui::Rect> UiSkinPreview::galleryBounds(NWB::Impl::UiPaintContext& context){
    using namespace __hidden_ui_skin_preview;
    const auto caption = m_caption.measure(context.text, context.display);
    const auto korean = m_korean.measure(context.text, context.display);
    if(!caption || !korean)
        return MakeUnexpected(Failure{});
    const SIMDVectorSingle captionHeight = VectorAdd(VectorAdd(VectorReplicate(caption->height), VectorReplicate(korean->height)), VectorReplicate(s_CaptionPadding));
    const f32 preferredTop = VectorGetX(VectorAdd(VectorAdd(VectorAdd(VectorReplicate(s_Margin), VectorReplicate(s_PanelBaseHeight)), captionHeight), VectorReplicate(s_PanelBottomGap)));
    return UiWidgetGallery::LayoutBounds(context.display, preferredTop);
}

void UiSkinPreview::paint(NWB::Impl::UiPaintContext& context){
    using namespace __hidden_ui_skin_preview;
    const SIMDVectorSingle margin = VectorReplicate(s_Margin);
    const f32 x = VectorGetX(VectorMax(margin, VectorSubtract(VectorSubtract(VectorReplicate(context.display.logicalWidth), VectorReplicate(s_PanelWidth)), margin)));
    const f32 y = s_Margin;
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
    const SIMDVectorSingle captionHeight = VectorAdd(VectorAdd(VectorReplicate(caption->height), VectorReplicate(korean->height)), VectorReplicate(s_CaptionPadding));
    const auto gallery = galleryBounds(context);
    if(!gallery){
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: UI gallery measurement failed"));
        return;
    }
    m_widgets.paint(context, *gallery);
    if(gallery->y < VectorGetX(VectorAdd(VectorAdd(VectorAdd(VectorReplicate(y), VectorReplicate(s_PanelBaseHeight)), captionHeight), VectorReplicate(s_PanelBottomGap))) || x < s_MinimumGalleryX)
        return;
    if(!DrawSkinGallery(context.paint, VectorSet(x, y, 0.0f, 0.0f), captionHeight)){
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: default UI skin is missing a gallery region"));
        return;
    }

    const SIMDVectorSingle origin = VectorSet(x, y, 0.0f, 0.0f);
    const SIMDVectorSingle captionOrigin = VectorAdd(origin, VectorSet(s_CaptionX, s_CaptionY, 0.0f, 0.0f));
    const SIMDVectorSingle koreanOffset = VectorAdd(VectorSet(s_CaptionX, s_CaptionY, 0.0f, 0.0f), VectorSet(0.0f, caption->height, 0.0f, 0.0f));
    const SIMDVectorSingle koreanOrigin = VectorAdd(origin, koreanOffset);
    const SIMDVectorSingle buttonTextOffsets = VectorScale(VectorSubtract(VectorReplicate(s_ButtonHeight), VectorSet(normal->height, hover->height, pressed->height, disabled->height)), 0.5f);
    const SIMDVectorSingle buttonTextY = VectorAdd(VectorSet(s_ButtonTopY, s_ButtonTopY, s_ButtonBottomY, s_ButtonBottomY), buttonTextOffsets);
    const SIMDVectorSingle normalOrigin = VectorAdd(origin, VectorSet(s_LeftTextX, VectorGetX(buttonTextY), 0.0f, 0.0f));
    const SIMDVectorSingle hoverOrigin = VectorAdd(origin, VectorSet(s_RightTextX, VectorGetY(buttonTextY), 0.0f, 0.0f));
    const SIMDVectorSingle pressedOrigin = VectorAdd(origin, VectorSet(s_LeftTextX, VectorGetZ(buttonTextY), 0.0f, 0.0f));
    const SIMDVectorSingle disabledOrigin = VectorAdd(origin, VectorSet(s_RightTextX, VectorGetW(buttonTextY), 0.0f, 0.0f));
    const SIMDVectorSingle editOffset = VectorAdd(VectorSet(s_LeftTextX, s_EditFocusedY, 0.0f, 0.0f), VectorScale(VectorSubtract(VectorSet(0.0f, s_EditFocusedHeight, 0.0f, 0.0f), VectorSet(0.0f, edit->height, 0.0f, 0.0f)), 0.5f));
    const SIMDVectorSingle editOrigin = VectorAdd(origin, editOffset);
    const bool captionPainted = m_caption.paint(context.text, context.paint, { VectorGetX(captionOrigin), VectorGetY(captionOrigin), s_CaptionWidth, caption->height })
        && m_korean.paint(context.text, context.paint, { VectorGetX(koreanOrigin), VectorGetY(koreanOrigin), s_CaptionWidth, korean->height });
    if(!captionPainted){
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom UI caption shaping or rasterization failed"));
        return;
    }
    if(
        !m_normal.paint(context.text, context.paint, { VectorGetX(normalOrigin), VectorGetY(normalOrigin), s_ButtonTextWidth, normal->height }, s_LabelTextColor)
        || !m_hover.paint(context.text, context.paint, { VectorGetX(hoverOrigin), VectorGetY(hoverOrigin), s_ButtonTextWidth, hover->height }, s_LabelTextColor)
        || !m_pressed.paint(context.text, context.paint, { VectorGetX(pressedOrigin), VectorGetY(pressedOrigin), s_ButtonTextWidth, pressed->height }, s_LabelTextColor)
        || !m_disabled.paint(context.text, context.paint, { VectorGetX(disabledOrigin), VectorGetY(disabledOrigin), s_ButtonTextWidth, disabled->height }, s_DisabledLabelTextColor)
        || !m_edit.paint(context.text, context.paint, { VectorGetX(editOrigin), VectorGetY(editOrigin), s_EditTextWidth, edit->height }, s_LabelTextColor)
    )
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom UI label shaping or rasterization failed"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void Project::drawCustomUiControls(NWB::Impl::UiPaintContext& context){
    m_uiPreview.paint(context);
}


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

