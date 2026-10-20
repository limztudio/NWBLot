// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "ui_slider_gallery.h"
#include "ui_gallery_constants.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void UiSliderGallery::paint(NWB::Impl::UiPaintContext& context, const f32 x, const f32 y){
    using namespace NWB::Impl::Ui;
    constexpr f32 s_PanelHeight = 180.0f;
    constexpr f64 s_KeyStep = 0.05;
    Builder& ui = context.ui;
    if(!ui.beginPanel("slider_gallery", { x, y, s_OrdinaryGalleryWidth, s_PanelHeight }))
        return;
    bool valid = ui.label(s_TitleKey, "Continuous slider");
    if(ui.checkbox(s_EnabledKey, "Enable slider", m_enabled))
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Testbed: custom slider enabled={}"), m_enabled);
    SliderOptions options;
    options.enabled = m_enabled;
    options.keyStep = s_KeyStep;
    valid = ui.slider(s_AmountKey, m_state, options) && valid;
    valid = ui.label(s_HintKey, "Drag, arrows, Home / End") && valid;
    valid = ui.endPanel() && valid;
    const SliderResult result = m_state.result();
    if(valid && result.valid && result.valueChanged)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Testbed: custom slider value={}"), m_state.value());
    if(!valid)
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom slider declaration failed"));
}


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

