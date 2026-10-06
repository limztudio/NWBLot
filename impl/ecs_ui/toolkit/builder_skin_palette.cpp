// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "builder.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_builder_skin_palette{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Color Resolve(const UiSkin& skin, const UiSkinColorRole::Enum role)noexcept{
    const UiSkinColor& value = skin.palette().colors[role];
    return { value.r, value.g, value.b, value.a };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void Builder::setSkin(const UiSkin& skin)noexcept{
    if(!balanced() || m_context.failed()){
        m_context.fail();
        return;
    }
    m_skin = &skin;
    const f32 nextDefaultFontSize = skin.typography().defaultFontSize;
    if(m_style.fontSize == m_skinDefaultFontSize)
        m_style.fontSize = nextDefaultFontSize;
    m_skinDefaultFontSize = nextDefaultFontSize;
    using namespace UiSkinColorRole;
    const auto resolve = [&skin](const Enum role)noexcept{
        return __hidden_ui_builder_skin_palette::Resolve(skin, role);
    };

    m_style.text = resolve(TextNormal);
    m_style.disabledText = resolve(TextDisabled);
    m_tooltipStyle.text = resolve(TextTooltip);
    m_editStyle.background = resolve(EditBackground);
    m_editStyle.text = resolve(TextNormal);
    m_editStyle.disabledText = resolve(TextDisabled);
    m_editStyle.selection = resolve(EditSelection);
    m_editStyle.inactiveSelection = resolve(EditInactiveSelection);
    m_editStyle.caret = resolve(EditCaret);
    m_editStyle.preedit = resolve(EditPreedit);
    m_scrollbarStyle.trackColor = resolve(ScrollbarTrack);
    m_scrollbarStyle.thumbColor = resolve(ScrollbarThumb);
    m_scrollbarStyle.disabledColor = resolve(ScrollbarDisabled);
    m_popupStyle.backdrop = resolve(PopupBackdrop);
    m_radioGroupStyle.hoverTint = resolve(ControlHoverTint);
    m_radioGroupStyle.pressedTint = resolve(ControlPressedTint);
    m_radioGroupStyle.disabledTint = resolve(ControlDisabledTint);
    m_sliderStyle.hoverTint = resolve(ControlHoverTint);
    m_sliderStyle.pressedTint = resolve(ControlPressedTint);
    m_sliderStyle.disabledTint = resolve(ControlDisabledTint);
    m_progressStyle.trackTint = resolve(ProgressTrackTint);
    m_progressStyle.fillTint = resolve(ProgressFillTint);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

