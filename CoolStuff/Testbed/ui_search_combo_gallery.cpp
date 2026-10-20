// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "ui_search_combo_gallery.h"
#include "ui_gallery_constants.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiSearchComboGallery::UiSearchComboGallery(NWB::Core::Alloc::GlobalArena& arena)
    : m_source(arena)
    , m_state(arena)
{}

void UiSearchComboGallery::paint(NWB::Impl::UiPaintContext& context, NWB::Core::Alloc::ScratchArena& scratchArena, const f32 x, const f32 y){
    using namespace NWB::Impl::Ui;
    constexpr f32 s_PanelHeight = 130.0f;
    constexpr f32 s_PopupHeight = 240.0f;
    Builder& ui = context.ui;
    if(!ui.beginPanel("search_combo_gallery", { x, y, s_OrdinaryGalleryWidth, s_PanelHeight }))
        return;
    const f32 previousFontSize = ui.style().fontSize;
    ui.style().fontSize = s_GalleryFontSize;
    bool valid = ui.label(s_TitleKey, "Searchable fruit selection");
    SearchComboOptions options;
    options.combo.popupHeight = s_PopupHeight;
    const SearchComboResult result = ui.searchComboBox(s_ChoiceKey, m_source, m_state, options);
    if(result.combo.committed)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Testbed: custom searchable combo selected key={}"), m_state.combo().selectedKey());
    const auto text = StringFormat(scratchArena, s_SelectedTextFormat, m_state.combo().selectedKey());
    valid = ui.label(s_SelectedKey, { text.data(), text.size() }) && result.combo.valid && valid;
    valid = ui.endPanel() && valid;
    ui.style().fontSize = previousFontSize;
    if(!valid)
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom searchable combo declaration failed"));
}


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

