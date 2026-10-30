// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "ui_radio_group_gallery.h"
#include "ui_gallery_constants.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void UiRadioGroupGallery::paint(NWB::Impl::UiPaintContext& context, const f32 x, const f32 y){
    using namespace NWB::Impl::Ui;
    constexpr f32 s_PanelHeight = 208.0f;
    Builder& ui = context.ui;
    if(!ui.beginPanel("radio_group_gallery", { x, y, s_OrdinaryGalleryWidth, s_PanelHeight }))
        return;
    b8 valid = ui.label(s_TitleKey, "Radio choices");
    const RadioGroupResult result = ui.radioGroup("quality", m_source, m_state);
    valid = ui.label(s_HintKey, "Arrows select; Enter / Space sets") && result.valid && valid;
    valid = ui.endPanel() && valid;
    if(result.activated)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Testbed: custom radio choice key={}"), m_state.selectedKey());
    if(!valid)
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom radio group declaration failed"));
}


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

