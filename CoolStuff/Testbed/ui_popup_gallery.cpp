// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "ui_popup_gallery.h"
#include "ui_gallery_constants.h"

#include <core/common/log.h>

#include <global/simd/vector_arithmetic.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_popup_gallery{
static constexpr f32 s_MenuButtonWidth = 60.0f;
static constexpr u32 s_FirstChoice = 1u;
static constexpr u32 s_SecondChoice = 2u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void UiPopupGallery::trigger(NWB::Impl::Ui::Builder& ui){
    using namespace NWB::Impl::Ui;
    const WidgetOptions options{ { LayoutSizePolicy::Fixed, __hidden_ui_popup_gallery::s_MenuButtonWidth }, {} };
    if(ui.button("menu", "Menu", options))
        m_popup.open();
}

void UiPopupGallery::paint(NWB::Impl::UiPaintContext& context, const f32 x, const f32 y){
    using namespace NWB::Impl::Ui;
    using namespace __hidden_ui_popup_gallery;
    constexpr f32 s_AnchorLeft = 196.0f;
    constexpr f32 s_AnchorTop = 68.0f;
    constexpr f32 s_AnchorHeight = 36.0f;
    constexpr f32 s_PopupWidth = 220.0f;
    constexpr f32 s_PopupHeight = 180.0f;
    Builder& ui = context.ui;
    PopupOptions options;
    const SIMDVectorSingle anchorOrigin = VectorAdd(VectorSet(x, y, 0.0f, 0.0f), VectorSet(s_AnchorLeft, s_AnchorTop, 0.0f, 0.0f));
    options.anchor = { VectorGetX(anchorOrigin), VectorGetY(anchorOrigin), s_MenuButtonWidth, s_AnchorHeight };
    options.size = { s_PopupWidth, s_PopupHeight };
    if(!ui.beginPopup("gallery_menu", m_popup, options))
        return;
    const f32 previousFontSize = ui.style().fontSize;
    ui.style().fontSize = s_GalleryFontSize;
    const WidgetOptions row{ { LayoutSizePolicy::Stretch, 1.0f }, {} };
    const StringView caption = m_choice == 0u ? "Popup menu" : m_choice == s_FirstChoice ? "First choice selected" : "Second choice selected";
    bool valid = ui.label(s_TitleKey, caption);
    if(ui.button("first", "First choice", row)){
        m_choice = s_FirstChoice;
        if(!m_keepOpen)
            m_popup.close();
    }
    if(ui.button("second", "Second choice", row)){
        m_choice = s_SecondChoice;
        if(!m_keepOpen)
            m_popup.close();
    }
    if(ui.checkbox("keep_open", "Keep open on selection", m_keepOpen, row))
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Testbed: popup keep open={}"), m_keepOpen);
    valid = ui.endPopup() && valid;
    ui.style().fontSize = previousFontSize;
    if(!valid)
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom popup declaration failed"));
}


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

