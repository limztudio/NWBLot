// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "ui_progress_gallery.h"
#include "ui_gallery_constants.h"

#include <core/common/log.h>

#include <global/simd/vector.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void UiProgressGallery::paint(NWB::Impl::UiPaintContext& context, NWB::Core::Alloc::ScratchArena& scratchArena, const f32 x, const f32 y){
    constexpr f32 s_PanelHeight = 180.0f;
    constexpr f64 s_AdvanceFraction = 0.25;
    constexpr f64 s_PercentScale = 100.0;
    NWB::Impl::Ui::Builder& ui = context.ui;
    if(!ui.beginPanel("progress_gallery", { x, y, s_OrdinaryGalleryWidth, s_PanelHeight }))
        return;
    bool valid = ui.label(s_TitleKey, "Skinned progress bar");
    if(ui.button("advance", "Advance by 25%"))
        m_fraction = m_fraction >= 1.0 ? 0.0 : VectorDoubleGetX(VectorDoubleAdd(VectorDoubleReplicate(m_fraction), VectorDoubleReplicate(s_AdvanceFraction)));
    valid = ui.progress(s_AmountKey, m_fraction) && valid;
    const auto caption = StringFormat(scratchArena, "Completed: {:.0f}%", VectorDoubleGetX(VectorDoubleScale(VectorDoubleReplicate(m_fraction), s_PercentScale)));
    valid = ui.label("value", { caption.data(), caption.size() }) && valid;
    valid = ui.endPanel() && valid;
    if(!valid)
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom progress declaration failed"));
}


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

