// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "ui_skin_preview.h"
#include "ui_gallery_constants.h"

#include <core/common/log.h>

#include <global/simd/vector_arithmetic.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_skin_preview{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using PaintBuilder = NWB::Impl::Ui::PaintBuilder;
using Label = NWB::Impl::Ui::Label;
using ShapeRequest = NWB::Impl::Ui::ShapeRequest;

static constexpr NWB::Impl::Ui::Color s_LabelTextColor{ 0.92f, 0.96f, 1.0f, 1.0f };
static constexpr NWB::Impl::Ui::Color s_DisabledLabelTextColor{ 0.38f, 0.42f, 0.48f, 1.0f };
static constexpr StringView s_KoreanText = "\xED\x95\x9C\xEA\xB8\x80 \xEC\xA1\xB0\xED\x95\xA9";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Origin lanes are x, y.
[[nodiscard]] static b8 DrawSkinGallery(PaintBuilder& paint, const SIMDVectorSingle origin, const f32 captionHeight){
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
        paint.drawRegion(s_Panel, { VectorGetX(origin), VectorGetY(origin), s_OrdinaryGalleryWidth, s_PanelBaseHeight + captionHeight })
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
        && paint.drawRegion(s_EditNormal, { VectorGetX(editNormal), VectorGetY(editNormal), s_EditNormalWidth, captionHeight })
    ;
}

[[nodiscard]] static b8 ConfigureLabel(const StringView text, Label& outLabel, const f32 size = s_LabelFontSize){
    return outLabel.setText(ShapeRequest{ .text = text, .fontSize = size }) == NWB::Impl::Ui::TextLayoutStatus::Success;
}

[[nodiscard]] static NWB::Impl::Ui::Rect ResolveGalleryRect(const NWB::Impl::Ui::DisplayMetrics& display, const f32 preferredTop)noexcept{
    const SIMDVectorSingle bounds = UiWidgetGallery::LayoutBounds(VectorSet(display.logicalWidth, display.logicalHeight, 0.0f, 0.0f), preferredTop);
    return { VectorGetX(bounds), VectorGetY(bounds), VectorGetZ(bounds), VectorGetW(bounds) };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiSkinPreview::UiSkinPreview(NWB::Core::Alloc::GlobalArena& arena)
    : m_normal(arena)
    , m_hover(arena)
    , m_pressed(arena)
    , m_disabled(arena)
    , m_edit(arena)
    , m_caption(arena)
    , m_korean(arena)
    , m_widgets(arena)
{
    using namespace __hidden_ui_skin_preview;
    const b8 configured = ConfigureLabel("Normal", m_normal) && ConfigureLabel("Hover", m_hover)
        && ConfigureLabel("Pressed", m_pressed) && ConfigureLabel("Disabled", m_disabled)
        && ConfigureLabel("UTF-8 labels", m_edit, s_GalleryFontSize) && ConfigureLabel("FreeType + HarfBuzz", m_caption, s_GalleryFontSize)
        && m_korean.setText(ShapeRequest{ .text = s_KoreanText, .language = "ko", .fontSize = s_LabelFontSize, .scriptTag = NWB::Impl::Ui::TextScriptTag('H', 'a', 'n', 'g') }) == NWB::Impl::Ui::TextLayoutStatus::Success;
    NWB_FATAL_ASSERT_MSG(configured, NWB_TEXT("Testbed UI labels must have valid constant text"));
}

Expected<NWB::Impl::Ui::Rect> UiSkinPreview::galleryBounds(NWB::Impl::UiPaintContext& context){
    using namespace __hidden_ui_skin_preview;
    const auto caption = m_caption.measure(context.text, context.display);
    const auto korean = m_korean.measure(context.text, context.display);
    if(!caption || !korean)
        return MakeUnexpected(Failure{});
    const f32 captionHeight = caption->height + korean->height + s_CaptionPadding;
    return ResolveGalleryRect(context.display, s_GalleryMargin + s_PanelBaseHeight + captionHeight + s_PanelBottomGap);
}

void UiSkinPreview::paint(NWB::Impl::UiPaintContext& context){
    using namespace __hidden_ui_skin_preview;
    const f32 x = Max(s_GalleryMargin, (context.display.logicalWidth - s_OrdinaryGalleryWidth) - s_GalleryMargin);
    const SIMDVectorSingle origin = VectorSet(x, s_GalleryMargin, 0.0f, 0.0f);
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
    const f32 captionHeight = caption->height + korean->height + s_CaptionPadding;
    const f32 preferredTop = s_GalleryMargin + s_PanelBaseHeight + captionHeight + s_PanelBottomGap;
    const NWB::Impl::Ui::Rect gallery = ResolveGalleryRect(context.display, preferredTop);
    m_widgets.paint(context, gallery);
    if(gallery.y < preferredTop || x < s_MinimumGalleryX)
        return;
    if(!DrawSkinGallery(context.paint, origin, captionHeight)){
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: default UI skin is missing a gallery region"));
        return;
    }

    const SIMDVectorSingle captionOrigin = VectorAdd(origin, VectorSet(s_CaptionX, s_CaptionY, 0.0f, 0.0f));
    const SIMDVectorSingle koreanOffset = VectorAdd(VectorSet(s_CaptionX, s_CaptionY, 0.0f, 0.0f), VectorSet(0.0f, caption->height, 0.0f, 0.0f));
    const SIMDVectorSingle koreanOrigin = VectorAdd(origin, koreanOffset);
    const SIMDVectorSingle buttonTextOffsets = VectorScale(VectorSubtract(VectorReplicate(s_ButtonHeight), VectorSet(normal->height, hover->height, pressed->height, disabled->height)), 0.5f);
    const SIMDVectorSingle buttonTextY = VectorAdd(VectorSet(s_ButtonTopY, s_ButtonTopY, s_ButtonBottomY, s_ButtonBottomY), buttonTextOffsets);
    const SIMDVectorSingle buttonTextX = VectorSet(s_LeftTextX, s_RightTextX, 0.0f, 0.0f);
    const SIMDVectorSingle normalOrigin = VectorAdd(origin, VectorPermute<0, 4, 2, 3>(buttonTextX, buttonTextY));
    const SIMDVectorSingle hoverOrigin = VectorAdd(origin, VectorPermute<1, 5, 2, 3>(buttonTextX, buttonTextY));
    const SIMDVectorSingle pressedOrigin = VectorAdd(origin, VectorPermute<0, 6, 2, 3>(buttonTextX, buttonTextY));
    const SIMDVectorSingle disabledOrigin = VectorAdd(origin, VectorPermute<1, 7, 2, 3>(buttonTextX, buttonTextY));
    const SIMDVectorSingle editOffset = VectorSet(s_LeftTextX, s_EditFocusedY + (s_EditFocusedHeight - edit->height) * 0.5f, 0.0f, 0.0f);
    const SIMDVectorSingle editOrigin = VectorAdd(origin, editOffset);
    const b8 captionPainted = m_caption.paint(context.text, context.paint, { VectorGetX(captionOrigin), VectorGetY(captionOrigin), s_CaptionWidth, caption->height })
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


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

