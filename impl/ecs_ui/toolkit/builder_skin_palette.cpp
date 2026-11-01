// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "builder.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


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
    const UiSkinPalette& palette = skin.palette();

    m_style.text = ResolveUiSkinColor(palette, TextNormal);
    m_style.disabledText = ResolveUiSkinColor(palette, TextDisabled);
    m_tooltipStyle.text = ResolveUiSkinColor(palette, TextTooltip);
    m_editStyle.background = ResolveUiSkinColor(palette, EditBackground);
    m_editStyle.text = ResolveUiSkinColor(palette, TextNormal);
    m_editStyle.disabledText = ResolveUiSkinColor(palette, TextDisabled);
    m_editStyle.selection = ResolveUiSkinColor(palette, EditSelection);
    m_editStyle.inactiveSelection = ResolveUiSkinColor(palette, EditInactiveSelection);
    m_editStyle.caret = ResolveUiSkinColor(palette, EditCaret);
    m_editStyle.preedit = ResolveUiSkinColor(palette, EditPreedit);
    m_scrollbarStyle.trackColor = ResolveUiSkinColor(palette, ScrollbarTrack);
    m_scrollbarStyle.thumbColor = ResolveUiSkinColor(palette, ScrollbarThumb);
    m_scrollbarStyle.disabledColor = ResolveUiSkinColor(palette, ScrollbarDisabled);
    m_popupStyle.backdrop = ResolveUiSkinColor(palette, PopupBackdrop);
    m_radioGroupStyle.hoverTint = ResolveUiSkinColor(palette, ControlHoverTint);
    m_radioGroupStyle.pressedTint = ResolveUiSkinColor(palette, ControlPressedTint);
    m_radioGroupStyle.disabledTint = ResolveUiSkinColor(palette, ControlDisabledTint);
    m_sliderStyle.hoverTint = ResolveUiSkinColor(palette, ControlHoverTint);
    m_sliderStyle.pressedTint = ResolveUiSkinColor(palette, ControlPressedTint);
    m_sliderStyle.disabledTint = ResolveUiSkinColor(palette, ControlDisabledTint);
    m_progressStyle.trackTint = ResolveUiSkinColor(palette, ProgressTrackTint);
    m_progressStyle.fillTint = ResolveUiSkinColor(palette, ProgressFillTint);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

