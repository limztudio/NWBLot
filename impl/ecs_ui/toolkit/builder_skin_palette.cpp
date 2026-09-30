// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "builder.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_builder_skin_palette{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Color Resolve(const UiSkin& skin, const UiSkinColorRole::Enum role, const Color& legacy){
    if(!skin.hasPalette())
        return legacy;
    const UiSkinColor& value = skin.palette().colors[role];
    return { value.r, value.g, value.b, value.a };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void Builder::setSkin(const UiSkin& skin){
    if(declarationBlocked()){
        m_context.fail();
        return;
    }
    m_skin = &skin;
    const f32 nextDefaultFontSize = skin.typography().defaultFontSize;
    if(m_style.fontSize == m_skinDefaultFontSize)
        m_style.fontSize = nextDefaultFontSize;
    m_skinDefaultFontSize = nextDefaultFontSize;
    using namespace UiSkinColorRole;
    const WidgetStyle widget;
    const EditBoxStyle edit;
    const ScrollbarStyle scrollbar;
    const PopupStyle popup;
    const RadioGroupStyle radio;
    const SliderStyle slider;
    const ProgressStyle progress;
    const TooltipStyle tooltip;
    const auto resolve = [&skin](const Enum role, const Color& legacy){
        return __hidden_ui_builder_skin_palette::Resolve(skin, role, legacy);
    };

    m_style.text = resolve(TextNormal, widget.text);
    m_style.disabledText = resolve(TextDisabled, widget.disabledText);
    m_tooltipStyle.text = resolve(TextTooltip, tooltip.text);
    m_editStyle.background = resolve(EditBackground, edit.background);
    m_editStyle.text = resolve(TextNormal, edit.text);
    m_editStyle.disabledText = resolve(TextDisabled, edit.disabledText);
    m_editStyle.selection = resolve(EditSelection, edit.selection);
    m_editStyle.inactiveSelection = resolve(EditInactiveSelection, edit.inactiveSelection);
    m_editStyle.caret = resolve(EditCaret, edit.caret);
    m_editStyle.preedit = resolve(EditPreedit, edit.preedit);
    m_scrollbarStyle.trackColor = resolve(ScrollbarTrack, scrollbar.trackColor);
    m_scrollbarStyle.thumbColor = resolve(ScrollbarThumb, scrollbar.thumbColor);
    m_scrollbarStyle.disabledColor = resolve(ScrollbarDisabled, scrollbar.disabledColor);
    m_popupStyle.backdrop = resolve(PopupBackdrop, popup.backdrop);
    m_radioGroupStyle.hoverTint = resolve(ControlHoverTint, radio.hoverTint);
    m_radioGroupStyle.pressedTint = resolve(ControlPressedTint, radio.pressedTint);
    m_radioGroupStyle.disabledTint = resolve(ControlDisabledTint, radio.disabledTint);
    m_sliderStyle.hoverTint = resolve(ControlHoverTint, slider.hoverTint);
    m_sliderStyle.pressedTint = resolve(ControlPressedTint, slider.pressedTint);
    m_sliderStyle.disabledTint = resolve(ControlDisabledTint, slider.disabledTint);
    m_progressStyle.trackTint = resolve(ProgressTrackTint, progress.trackTint);
    m_progressStyle.fillTint = resolve(ProgressFillTint, progress.fillTint);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

