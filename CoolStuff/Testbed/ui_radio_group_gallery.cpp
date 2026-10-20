// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "ui_radio_group_gallery.h"
#include "ui_gallery_constants.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<u64> UiRadioGroupSource::indexOf(const u64 keyValue)const noexcept{
    if(keyValue == 0u || keyValue > s_RowCount)
        return MakeUnexpected(Failure{});
    return keyValue - 1u;
}

Expected<u64> UiRadioGroupSource::findEnabled(const u64 start, const bool reverse)const noexcept{
    if(start >= s_RowCount)
        return MakeUnexpected(Failure{});
    const u64 index = start == s_FirstDisabledIndex ? (reverse ? s_LastEnabledIndex : s_RowCount) : start;
    if(index >= s_RowCount)
        return MakeUnexpected(Failure{});
    return index;
}

StringView UiRadioGroupSource::text(const u64 index)const noexcept{
    static constexpr StringView s_Labels[] = { "Balanced", "High quality", "Unavailable" };
    return index < s_RowCount ? s_Labels[index] : StringView{};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void UiRadioGroupGallery::paint(NWB::Impl::UiPaintContext& context, const f32 x, const f32 y){
    using namespace NWB::Impl::Ui;
    constexpr f32 s_PanelHeight = 208.0f;
    Builder& ui = context.ui;
    if(!ui.beginPanel("radio_group_gallery", { x, y, s_OrdinaryGalleryWidth, s_PanelHeight }))
        return;
    bool valid = ui.label(s_TitleKey, "Radio choices");
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

