// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "ui_radio_group_gallery.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u64 TestbedUiRadioGroupSource::instanceGeneration()const{ return 1u; }

u64 TestbedUiRadioGroupSource::revision()const{ return 1u; }

u64 TestbedUiRadioGroupSource::rowCount()const{ return 3u; }

u64 TestbedUiRadioGroupSource::key(const u64 index)const{ return index < 3u ? index + 1u : 0u; }

bool TestbedUiRadioGroupSource::indexOf(const u64 keyValue, u64& index)const{
    if(keyValue == 0u || keyValue > 3u)
        return false;
    index = keyValue - 1u;
    return true;
}

bool TestbedUiRadioGroupSource::findEnabled(const u64 start, const bool reverse, u64& index)const{
    if(start >= 3u)
        return false;
    index = start == 2u ? (reverse ? 1u : 3u) : start;
    return index < 3u;
}

StringView TestbedUiRadioGroupSource::text(const u64 index)const{
    static constexpr StringView s_Labels[] = { "Balanced", "High quality", "Unavailable" };
    return index < 3u ? s_Labels[index] : StringView{};
}

bool TestbedUiRadioGroupSource::enabled(const u64 index)const{ return index < 2u; }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void TestbedUiRadioGroupGallery::paint(NWB::Impl::UiPaintContext& context, const f32 x, const f32 y){
    using namespace NWB::Impl::Ui;
    Builder& ui = context.ui;
    if(!ui.beginPanel("radio_group_gallery", { x, y, 280.0f, 208.0f }))
        return;
    bool valid = ui.label("title", "Radio choices");
    const RadioGroupResult result = ui.radioGroup("quality", m_source, m_state);
    valid = ui.label("hint", "Arrows select; Enter / Space sets") && result.valid && valid;
    valid = ui.endPanel() && valid;
    if(result.activated)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Testbed: custom radio choice key={}"), m_state.selectedKey());
    if(!valid)
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom radio group declaration failed"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

