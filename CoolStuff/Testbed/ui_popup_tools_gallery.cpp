// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "ui_popup_tools_gallery.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void TestbedUiPopupToolsGallery::paint(NWB::Impl::UiPaintContext& context, const f32 x, const f32 y){
    using namespace NWB::Impl::Ui;
    Builder& ui = context.ui;
    if(!ui.beginPanel("popup_tools_gallery", { x, y, 280.0f, 132.0f }))
        return;
    const f32 previousFontSize = ui.style().fontSize;
    ui.style().fontSize = 14.0f;
    const WidgetOptions caption{ {}, { LayoutSizePolicy::Fixed, 20.0f } };
    bool valid = ui.label("title", "Tooltip and context commands", caption);
    const WidgetOptions anchor{ {}, { LayoutSizePolicy::Fixed, 32.0f } };
    if(ui.button("anchor", "Right-click / Menu / Shift+F10", anchor))
        ++m_anchorClicks;
    valid = ui.tooltip("help", "anchor", "Open commands here; Delete is disabled.", m_tooltip) && valid;
    ContextMenuOptions options;
    options.size = { 220.0f, 180.0f };
    const ContextMenuResult result = ui.contextMenu("commands", "anchor", m_source, m_menu, options);
    if(result.activated){
        m_command = result.key;
        NWB_LOGGER_ESSENTIAL_INFO(GLB_TEXT("Testbed: custom context command key={}"), m_command);
    }
    NWB::Core::Alloc::ScratchArena scratchArena(Name("testbed/ui/popup_tools_caption"));
    const auto text = StringFormat(scratchArena, "Command: {} / Clicks: {}", m_command, m_anchorClicks);
    valid = ui.label("selected", { text.data(), text.size() }, caption) && result.valid && valid;
    valid = ui.endPanel() && valid;
    ui.style().fontSize = previousFontSize;
    if(!valid)
        NWB_LOGGER_ERROR(GLB_TEXT("Testbed: custom popup tools declaration failed"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

