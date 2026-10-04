// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "ui_combo_gallery.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void TestbedUiComboGallery::paint(NWB::Impl::UiPaintContext& context, const f32 x, const f32 y){
    using namespace NWB::Impl::Ui;
    Builder& ui = context.ui;
    if(!ui.beginPanel("combo_gallery", { x, y, 280.0f, 130.0f }))
        return;
    const f32 previousFontSize = ui.style().fontSize;
    ui.style().fontSize = 14.0f;
    const WidgetOptions caption{ {}, { LayoutSizePolicy::Fixed, 20.0f } };
    bool valid = ui.label("title", "Keyed combo selection", caption);
    ComboOptions options;
    options.height = { LayoutSizePolicy::Fixed, 32.0f };
    options.rowHeight = 24.0f;
    options.popupHeight = 220.0f;
    const ComboResult result = ui.comboBox("choice", m_source, m_state, options);
    if(result.committed)
        NWB_LOGGER_ESSENTIAL_INFO(GLB_TEXT("Testbed: custom combo selected key={}"), m_state.selectedKey());
    NWB::Core::Alloc::ScratchArena scratchArena(Name("testbed/ui/combo_caption"));
    const auto text = StringFormat(scratchArena, "Selected: {}", m_state.selectedKey());
    valid = ui.label("selected", { text.data(), text.size() }, caption) && result.valid && valid;
    valid = ui.endPanel() && valid;
    ui.style().fontSize = previousFontSize;
    if(!valid)
        NWB_LOGGER_ERROR(GLB_TEXT("Testbed: custom combo declaration failed"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

