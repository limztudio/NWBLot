// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "ui_progress_gallery.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void UiProgressGallery::paint(NWB::Impl::UiPaintContext& context, const f32 x, const f32 y){
    NWB::Impl::Ui::Builder& ui = context.ui;
    if(!ui.beginPanel("progress_gallery", { x, y, 280.0f, 180.0f }))
        return;
    bool valid = ui.label("title", "Skinned progress bar");
    if(ui.button("advance", "Advance by 25%"))
        m_fraction = m_fraction >= 1.0 ? 0.0 : m_fraction + 0.25;
    valid = ui.progress("amount", m_fraction) && valid;
    NWB::Core::Alloc::ScratchArena scratchArena(Name("testbed/ui/progress_caption"));
    const auto caption = StringFormat(scratchArena, "Completed: {:.0f}%", m_fraction * 100.0);
    valid = ui.label("value", { caption.data(), caption.size() }) && valid;
    valid = ui.endPanel() && valid;
    if(!valid)
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom progress declaration failed"));
}


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

