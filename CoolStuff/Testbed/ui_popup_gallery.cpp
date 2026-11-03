// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "ui_popup_gallery.h"
#include "ui_gallery_support.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void UiPopupGallery::trigger(NWB::Impl::Ui::Builder& ui){
    using namespace NWB::Impl::Ui;
    const WidgetOptions options{ { LayoutSizePolicy::Fixed, s_PopupMenuButtonWidth }, {} };
    if(ui.button("menu", "Menu", options))
        m_popup.open();
}

void UiPopupGallery::paint(NWB::Impl::UiPaintContext& context, const f32 x, const f32 y){
    using namespace NWB::Impl::Ui;
    Builder& ui = context.ui;
    PopupOptions options;
    options.anchor = { x + s_PopupAnchorLeft, y + s_PopupAnchorTop, s_PopupMenuButtonWidth, s_PopupAnchorHeight };
    options.size = { s_PopupGalleryWidth, s_PopupGalleryHeight };
    if(!ui.beginPopup("gallery_menu", m_popup, options))
        return;
    const b8 valid = WithGalleryFontSize(ui, [&](){
        const WidgetOptions row{ { LayoutSizePolicy::Stretch, 1.0f }, {} };
        const StringView caption = ResolvePopupGalleryCaption(m_choice);
        b8 declared = ui.label(s_TitleKey, caption);
        if(ui.button("first", "First choice", row)){
            m_choice = s_PopupFirstChoice;
            if(!m_keepOpen)
                m_popup.close();
        }
        if(ui.button("second", "Second choice", row)){
            m_choice = s_PopupSecondChoice;
            if(!m_keepOpen)
                m_popup.close();
        }
        if(ui.checkbox("keep_open", "Keep open on selection", m_keepOpen, row))
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Testbed: popup keep open={}"), m_keepOpen);
        return ui.endPopup() && declared;
    });
    if(!valid)
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom popup declaration failed"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

