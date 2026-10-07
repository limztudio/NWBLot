// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "ui_edit_gallery.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiEditGallery::UiEditGallery(NWB::Core::Alloc::GlobalArena& arena)
    : m_text(arena)
    , m_readOnly(arena)
{
    const bool configured = m_text.setText("Edit me: \xED\x95\x9C\xEA\xB8\x80")
        && m_readOnly.setText("Read-only: copy this text");
    NWB_FATAL_ASSERT_MSG(configured, NWB_TEXT("Testbed edit gallery must have valid initial text"));
}

void UiEditGallery::paint(NWB::Impl::UiPaintContext& context, const f32 x, const f32 y){
    using namespace NWB::Impl::Ui;
    Builder& ui = context.ui;
    if(!ui.beginPanel("edit_gallery", { x, y, 280.0f, 154.0f }))
        return;
    const f32 previousFontSize = ui.style().fontSize;
    ui.style().fontSize = 14.0f;
    const WidgetOptions caption{ {}, { LayoutSizePolicy::Fixed, 20.0f } };
    bool valid = ui.label("title", "Text editing", caption);
    EditBoxOptions options;
    options.height = { LayoutSizePolicy::Fixed, 36.0f };
    const EditBoxResult text = ui.editBox("editable", m_text, m_textState, options);
    if(text.textChanged)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Testbed: custom edit text bytes={}"), m_text.text().size());
    options.readOnly = true;
    const EditBoxResult readOnly = ui.editBox("read_only", m_readOnly, m_readOnlyState, options);
    valid = text.valid && readOnly.valid && valid;
    valid = ui.label("hint", "Ctrl+A/C/X/V, undo and redo", caption) && valid;
    valid = ui.endPanel() && valid;
    ui.style().fontSize = previousFontSize;
    if(!valid)
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom edit widget declaration failed"));
}


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

