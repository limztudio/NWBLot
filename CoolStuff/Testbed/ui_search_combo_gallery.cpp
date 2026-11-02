// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "ui_search_combo_gallery.h"
#include "ui_gallery_support.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void UiSearchComboGallery::paint(NWB::Impl::UiPaintContext& context, NWB::Core::Alloc::ScratchArena& scratchArena, const f32 x, const f32 y){
    using namespace NWB::Impl::Ui;
    Builder& ui = context.ui;
    if(!ui.beginPanel("search_combo_gallery", { x, y, s_OrdinaryGalleryWidth, s_SearchComboPanelHeight }))
        return;
    const b8 valid = WithGalleryFontSize(ui, [&](){
        b8 declared = ui.label(s_TitleKey, "Searchable fruit selection");
        SearchComboOptions options;
        options.combo.popupHeight = s_SearchComboPopupHeight;
        const SearchComboResult result = ui.searchComboBox(s_ChoiceKey, m_source, m_state, options);
        if(result.combo.committed)
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Testbed: custom searchable combo selected key={}"), m_state.combo().selectedKey());
        const auto text = StringFormat(scratchArena, s_SelectedTextFormat, m_state.combo().selectedKey());
        declared = ui.label(s_SelectedKey, { text.data(), text.size() }) && result.combo.valid && declared;
        return ui.endPanel() && declared;
    });
    if(!valid)
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom searchable combo declaration failed"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

