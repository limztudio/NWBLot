// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "ui_popup_gallery.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void TestbedUiPopupGallery::trigger(NWB::Impl::Ui::Builder& ui){
    using namespace NWB::Impl::Ui;
    const WidgetOptions options{ { LayoutSizePolicy::Fixed, 60.0f }, {} };
    if(ui.button("menu", "Menu", options))
        m_popup.open();
}

void TestbedUiPopupGallery::paint(NWB::Impl::UiPaintContext& context, const f32 x, const f32 y){
    using namespace NWB::Impl::Ui;
    Builder& ui = context.ui;
    PopupOptions options;
    options.anchor = { x + 196.0f, y + 68.0f, 60.0f, 36.0f };
    options.size = { 220.0f, 180.0f };
    if(!ui.beginPopup("gallery_menu", m_popup, options))
        return;
    const f32 previousFontSize = ui.style().fontSize;
    ui.style().fontSize = 14.0f;
    const WidgetOptions title{ {}, { LayoutSizePolicy::Fixed, 20.0f } };
    const WidgetOptions row{ { LayoutSizePolicy::Stretch, 1.0f }, { LayoutSizePolicy::Fixed, 32.0f } };
    const StringView caption = m_choice == 0u ? "Popup menu" : m_choice == 1u ? "First choice selected" : "Second choice selected";
    bool valid = ui.label("title", caption, title);
    if(ui.button("first", "First choice", row)){
        m_choice = 1u;
        if(!m_keepOpen)
            m_popup.close();
    }
    if(ui.button("second", "Second choice", row)){
        m_choice = 2u;
        if(!m_keepOpen)
            m_popup.close();
    }
    if(ui.checkbox("keep_open", "Keep open on selection", m_keepOpen, row))
        NWB_LOGGER_ESSENTIAL_INFO(GLB_TEXT("Testbed: popup keep open={}"), m_keepOpen);
    valid = ui.endPopup() && valid;
    ui.style().fontSize = previousFontSize;
    if(!valid)
        NWB_LOGGER_ERROR(GLB_TEXT("Testbed: custom popup declaration failed"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

