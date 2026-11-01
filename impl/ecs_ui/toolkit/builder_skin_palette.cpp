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
    const UiSkinPalette& palette = skin.palette();

    ApplySkinPaletteToWidgetStyle(palette, m_style);
    ApplySkinPaletteToTooltipStyle(palette, m_tooltipStyle);
    ApplySkinPaletteToEditBoxStyle(palette, m_editStyle);
    ApplySkinPaletteToScrollbarStyle(palette, m_scrollbarStyle);
    ApplySkinPaletteToPopupStyle(palette, m_popupStyle);
    ApplySkinPaletteToRadioGroupStyle(palette, m_radioGroupStyle);
    ApplySkinPaletteToSliderStyle(palette, m_sliderStyle);
    ApplySkinPaletteToProgressStyle(palette, m_progressStyle);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

