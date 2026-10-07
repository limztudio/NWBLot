// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "ui_search_combo_gallery.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiSearchComboGallery::UiSearchComboGallery(NWB::Core::Alloc::GlobalArena& arena)
    : m_source(arena)
    , m_state(arena)
{}

void UiSearchComboGallery::paint(NWB::Impl::UiPaintContext& context, const f32 x, const f32 y){
    using namespace NWB::Impl::Ui;
    Builder& ui = context.ui;
    if(!ui.beginPanel("search_combo_gallery", { x, y, 280.0f, 130.0f }))
        return;
    const f32 previousFontSize = ui.style().fontSize;
    ui.style().fontSize = 14.0f;
    const WidgetOptions caption{ {}, { LayoutSizePolicy::Fixed, 20.0f } };
    bool valid = ui.label("title", "Searchable fruit selection", caption);
    SearchComboOptions options;
    options.combo.height = { LayoutSizePolicy::Fixed, 32.0f };
    options.combo.rowHeight = 24.0f;
    options.combo.popupHeight = 240.0f;
    const SearchComboResult result = ui.searchComboBox("choice", m_source, m_state, options);
    if(result.combo.committed)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Testbed: custom searchable combo selected key={}"), m_state.combo().selectedKey());
    NWB::Core::Alloc::ScratchArena scratchArena(Name("testbed/ui/search_combo_caption"));
    const auto text = StringFormat(scratchArena, "Selected: {}", m_state.combo().selectedKey());
    valid = ui.label("selected", { text.data(), text.size() }, caption) && result.combo.valid && valid;
    valid = ui.endPanel() && valid;
    ui.style().fontSize = previousFontSize;
    if(!valid)
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom searchable combo declaration failed"));
}


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

