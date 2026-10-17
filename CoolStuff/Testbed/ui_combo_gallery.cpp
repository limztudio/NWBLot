// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "ui_combo_gallery.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void UiComboGallery::paint(NWB::Impl::UiPaintContext& context, NWB::Core::Alloc::ScratchArena& scratchArena, const f32 x, const f32 y){
    using namespace NWB::Impl::Ui;
    Builder& ui = context.ui;
    if(!ui.beginPanel("combo_gallery", { x, y, 280.0f, 130.0f }))
        return;
    const f32 previousFontSize = ui.style().fontSize;
    ui.style().fontSize = 14.0f;
    bool valid = ui.label("title", "Keyed combo selection");
    ComboOptions options;
    options.popupHeight = 220.0f;
    const ComboResult result = ui.comboBox("choice", m_source, m_state, options);
    if(result.committed)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Testbed: custom combo selected key={}"), m_state.selectedKey());
    const auto text = StringFormat(scratchArena, "Selected: {}", m_state.selectedKey());
    valid = ui.label("selected", { text.data(), text.size() }) && result.valid && valid;
    valid = ui.endPanel() && valid;
    ui.style().fontSize = previousFontSize;
    if(!valid)
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom combo declaration failed"));
}


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

