// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "ui_slider_gallery.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void UiSliderGallery::paint(NWB::Impl::UiPaintContext& context, const f32 x, const f32 y){
    using namespace NWB::Impl::Ui;
    Builder& ui = context.ui;
    if(!ui.beginPanel("slider_gallery", { x, y, 280.0f, 180.0f }))
        return;
    bool valid = ui.label("title", "Continuous slider");
    if(ui.checkbox("enabled", "Enable slider", m_enabled))
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Testbed: custom slider enabled={}"), m_enabled);
    SliderOptions options;
    options.enabled = m_enabled;
    options.keyStep = 0.05;
    valid = ui.slider("amount", m_state, options) && valid;
    valid = ui.label("hint", "Drag, arrows, Home / End") && valid;
    valid = ui.endPanel() && valid;
    const SliderResult result = m_state.result();
    if(valid && result.valid && result.valueChanged)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Testbed: custom slider value={}"), m_state.value());
    if(!valid)
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom slider declaration failed"));
}


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

