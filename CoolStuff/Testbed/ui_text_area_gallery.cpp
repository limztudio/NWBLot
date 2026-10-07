// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "ui_text_area_gallery.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiTextAreaGallery::UiTextAreaGallery(NWB::Core::Alloc::GlobalArena& arena)
    : m_document(arena, {}, NWB::Impl::Ui::EditTextMode::Multiline)
{
    const bool initialized = m_document.setText("Custom multiline editor\nUp / Down retain the preferred column.\nShift selects; Ctrl+Enter submits.\nClipboard and IME belong to the OS.\n\nLong lines scroll horizontally; longer documents scroll vertically.");
    NWB_FATAL_ASSERT(initialized);
}

void UiTextAreaGallery::paint(NWB::Impl::UiPaintContext& context, const f32 x, const f32 y){
    using namespace NWB::Impl::Ui;
    Builder& ui = context.ui;
    if(!ui.beginPanel("text_area_gallery", { x, y, 400.0f, 260.0f }))
        return;
    const f32 previousFontSize = ui.style().fontSize;
    ui.style().fontSize = 14.0f;
    const WidgetOptions caption{ {}, { LayoutSizePolicy::Fixed, 20.0f } };
    bool valid = ui.label("title", "Multiline text area", caption);
    if(ui.checkbox("read_only", "Read only", m_readOnly, caption))
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Testbed: text area read only={}"), m_readOnly);
    TextAreaOptions options;
    options.readOnly = m_readOnly;
    const EditBoxResult result = ui.textArea("document", m_document, m_state, options);
    valid = result.valid && ui.label("hint", "Home / End: line; Ctrl: whole document", caption) && valid;
    valid = ui.endPanel() && valid;
    ui.style().fontSize = previousFontSize;
    if(result.submitted)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Testbed: multiline document submitted, bytes={}"), m_document.text().size());
    if(!valid)
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom text area declaration failed"));
}


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

