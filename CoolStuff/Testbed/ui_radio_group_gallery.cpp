// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "ui_radio_group_gallery.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u64 UiRadioGroupSource::instanceGeneration()const noexcept{ return 1u; }

u64 UiRadioGroupSource::revision()const noexcept{ return 1u; }

u64 UiRadioGroupSource::rowCount()const noexcept{ return 3u; }

u64 UiRadioGroupSource::key(const u64 index)const noexcept{ return index < 3u ? index + 1u : 0u; }

Expected<u64> UiRadioGroupSource::indexOf(const u64 keyValue)const noexcept{
    if(keyValue == 0u || keyValue > 3u)
        return MakeUnexpected(Failure{});
    return keyValue - 1u;
}

Expected<u64> UiRadioGroupSource::findEnabled(const u64 start, const bool reverse)const noexcept{
    if(start >= 3u)
        return MakeUnexpected(Failure{});
    const u64 index = start == 2u ? (reverse ? 1u : 3u) : start;
    if(index >= 3u)
        return MakeUnexpected(Failure{});
    return index;
}

StringView UiRadioGroupSource::text(const u64 index)const noexcept{
    static constexpr StringView s_Labels[] = { "Balanced", "High quality", "Unavailable" };
    return index < 3u ? s_Labels[index] : StringView{};
}

bool UiRadioGroupSource::enabled(const u64 index)const noexcept{ return index < 2u; }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void UiRadioGroupGallery::paint(NWB::Impl::UiPaintContext& context, const f32 x, const f32 y){
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


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

