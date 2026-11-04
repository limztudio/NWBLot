// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "ui_skin_preview.h"
#include "CoolStuff/Testbed/ui_gallery_constants.adhd.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_skin_preview{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using PaintBuilder = NWB::Impl::Ui::PaintBuilder;
using Label = NWB::Impl::Ui::Label;
using ShapeRequest = NWB::Impl::Ui::ShapeRequest;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static b8 DrawSkinGallery(PaintBuilder& paint, const f32 originX, const f32 originY, const f32 captionHeight){
    const f32 leftColumnX = originX + s_LeftColumnX;
    const f32 rightColumnX = originX + s_RightColumnX;
    const f32 buttonTopY = originY + s_ButtonTopY;
    const f32 buttonBottomY = originY + s_ButtonBottomY;
    return
        paint.drawRegion(s_Panel, { originX, originY, s_OrdinaryGalleryWidth, s_PanelBaseHeight + captionHeight })
        && paint.drawRegion(s_ButtonNormal, { leftColumnX, buttonTopY, s_ButtonWidth, s_ButtonHeight })
        && paint.drawRegion(s_ButtonHover, { rightColumnX, buttonTopY, s_ButtonWidth, s_ButtonHeight })
        && paint.drawRegion(s_ButtonPressed, { leftColumnX, buttonBottomY, s_ButtonWidth, s_ButtonHeight })
        && paint.drawRegion(s_ButtonDisabled, { rightColumnX, buttonBottomY, s_ButtonWidth, s_ButtonHeight })
        && paint.drawRegion(s_EditFocused, { leftColumnX, originY + s_EditFocusedY, s_EditFocusedWidth, s_EditFocusedHeight })
        && paint.drawRegion(s_ComboArrow, { originX + s_ComboArrowX, originY + s_ComboArrowY, s_ComboArrowSize, s_ComboArrowSize })
        && paint.drawRegion(s_Checkbox, { originX + s_CheckboxX, originY + s_CheckboxY, s_CheckboxSize, s_CheckboxSize })
        && paint.drawRegion(s_CheckboxMark, { originX + s_CheckboxMarkX, originY + s_CheckboxMarkY, s_CheckboxMarkSize, s_CheckboxMarkSize })
        && paint.drawRegion(s_ProgressTrack, { leftColumnX, originY + s_ProgressTrackY, s_ProgressTrackWidth, s_ProgressTrackHeight })
        && paint.drawRegion(s_ProgressFill, { originX + s_ProgressFillX, originY + s_ProgressFillY, s_ProgressFillWidth, s_ProgressFillHeight })
        && paint.drawRegion(s_EditNormal, { leftColumnX, originY + s_EditNormalY, s_EditNormalWidth, captionHeight })
    ;
}

[[nodiscard]] static b8 ConfigureLabel(const StringView text, const f32 size, Label& outLabel){
    return outLabel.setText(ShapeRequest{ .text = text, .fontSize = size }) == NWB::Impl::Ui::TextLayoutStatus::Success;
}

[[nodiscard]] static b8 ConfigureLabel(const StringView text, Label& outLabel){
    return ConfigureLabel(text, s_LabelFontSize, outLabel);
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
        && ConfigureLabel("UTF-8 labels", s_GalleryFontSize, m_edit) && ConfigureLabel("FreeType + HarfBuzz", s_GalleryFontSize, m_caption)
        && m_korean.setText(ShapeRequest{ .text = s_KoreanText, .language = "ko", .fontSize = s_LabelFontSize, .scriptTag = ::PackFourCharTag('H', 'a', 'n', 'g') }) == NWB::Impl::Ui::TextLayoutStatus::Success;
    NWB_FATAL_ASSERT_MSG(configured, NWB_TEXT("Testbed UI labels must have valid constant text"));
}

Expected<NWB::Impl::Ui::Rect> UiSkinPreview::galleryBounds(NWB::Impl::UiPaintContext& context){
    using namespace __hidden_ui_skin_preview;
    const auto caption = m_caption.measure(context.text, context.display);
    const auto korean = m_korean.measure(context.text, context.display);
    if(!caption || !korean)
        return MakeUnexpected(Failure{});
    const f32 captionHeight = ResolveSkinGalleryCaptionHeight(caption->height, korean->height);
    return UiWidgetGallery::LayoutBounds(context.display, ResolveSkinGalleryPreferredTop(captionHeight));
}

void UiSkinPreview::paint(NWB::Impl::UiPaintContext& context){
    using namespace __hidden_ui_skin_preview;
    const f32 originX = ResolveSkinGalleryOriginX(context.display.logicalWidth);
    const f32 originY = s_GalleryMargin;
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
    const f32 captionHeight = ResolveSkinGalleryCaptionHeight(caption->height, korean->height);
    const f32 preferredTop = ResolveSkinGalleryPreferredTop(captionHeight);
    const NWB::Impl::Ui::Rect gallery = UiWidgetGallery::LayoutBounds(context.display, preferredTop);
    m_widgets.paint(context, gallery);
    if(gallery.y < preferredTop || originX < s_MinimumGalleryX)
        return;
    if(!DrawSkinGallery(context.paint, originX, originY, captionHeight)){
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: default UI skin is missing a gallery region"));
        return;
    }

    const f32 captionX = originX + s_CaptionX;
    const f32 captionY = originY + s_CaptionY;
    const f32 leftTextX = originX + s_LeftTextX;
    const f32 rightTextX = originX + s_RightTextX;
    const f32 normalY = ResolveSkinGalleryCenteredTextY(originY, s_ButtonTopY, s_ButtonHeight, normal->height);
    const f32 hoverY = ResolveSkinGalleryCenteredTextY(originY, s_ButtonTopY, s_ButtonHeight, hover->height);
    const f32 pressedY = ResolveSkinGalleryCenteredTextY(originY, s_ButtonBottomY, s_ButtonHeight, pressed->height);
    const f32 disabledY = ResolveSkinGalleryCenteredTextY(originY, s_ButtonBottomY, s_ButtonHeight, disabled->height);
    const f32 editY = ResolveSkinGalleryCenteredTextY(originY, s_EditFocusedY, s_EditFocusedHeight, edit->height);
    const b8 captionPainted = m_caption.paint(context.text, context.paint, { captionX, captionY, s_CaptionWidth, caption->height })
        && m_korean.paint(context.text, context.paint, { captionX, captionY + caption->height, s_CaptionWidth, korean->height });
    if(!captionPainted){
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom UI caption shaping or rasterization failed"));
        return;
    }
    if(
        !m_normal.paint(context.text, context.paint, { leftTextX, normalY, s_ButtonTextWidth, normal->height }, s_LabelTextColor)
        || !m_hover.paint(context.text, context.paint, { rightTextX, hoverY, s_ButtonTextWidth, hover->height }, s_LabelTextColor)
        || !m_pressed.paint(context.text, context.paint, { leftTextX, pressedY, s_ButtonTextWidth, pressed->height }, s_LabelTextColor)
        || !m_disabled.paint(context.text, context.paint, { rightTextX, disabledY, s_ButtonTextWidth, disabled->height }, s_DisabledLabelTextColor)
        || !m_edit.paint(context.text, context.paint, { leftTextX, editY, s_EditTextWidth, edit->height }, s_LabelTextColor)
    )
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom UI label shaping or rasterization failed"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

