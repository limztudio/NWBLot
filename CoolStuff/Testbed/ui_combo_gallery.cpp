// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "ui_combo_gallery.h"
#include "ui_gallery_support.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void UiComboGallery::paint(NWB::Impl::UiPaintContext& context, NWB::Core::Alloc::ScratchArena& scratchArena, const f32 x, const f32 y){
    using namespace NWB::Impl::Ui;
    Builder& ui = context.ui;
    if(!ui.beginPanel("combo_gallery", { x, y, s_OrdinaryGalleryWidth, s_ComboGalleryPanelHeight }))
        return;
    const b8 valid = WithGalleryFontSize(ui, [&](){
        b8 declared = ui.label(s_TitleKey, "Keyed combo selection");
        ComboOptions options;
        options.popupHeight = s_ComboGalleryPopupHeight;
        const ComboResult result = ui.comboBox(s_ChoiceKey, m_source, m_state, options);
        if(result.committed)
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Testbed: custom combo selected key={}"), m_state.selectedKey());
        declared = DeclareSelectedKeyLabel(ui, scratchArena, m_state.selectedKey()) && result.valid && declared;
        return ui.endPanel() && declared;
    });
    if(!valid)
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom combo declaration failed"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

