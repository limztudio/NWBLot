// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "ui_popup_tools_gallery.h"
#include "ui_gallery_constants.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void UiPopupToolsGallery::paint(NWB::Impl::UiPaintContext& context, NWB::Core::Alloc::ScratchArena& scratchArena, const f32 x, const f32 y){
    using namespace NWB::Impl::Ui;
    constexpr f32 s_PanelHeight = 132.0f;
    constexpr f32 s_MenuWidth = 220.0f;
    constexpr f32 s_MenuHeight = 180.0f;
    constexpr AStringView s_AnchorKey = "anchor";
    Builder& ui = context.ui;
    if(!ui.beginPanel("popup_tools_gallery", { x, y, s_OrdinaryGalleryWidth, s_PanelHeight }))
        return;
    const f32 previousFontSize = ui.style().fontSize;
    ui.style().fontSize = s_GalleryFontSize;
    bool valid = ui.label(s_TitleKey, "Tooltip and context commands");
    if(ui.button(s_AnchorKey, "Right-click / Menu / Shift+F10"))
        ++m_anchorClicks;
    valid = ui.tooltip("help", s_AnchorKey, "Open commands here; Delete is disabled.", m_tooltip) && valid;
    ContextMenuOptions options;
    options.size = { s_MenuWidth, s_MenuHeight };
    const ContextMenuResult result = ui.contextMenu(s_CommandsKey, s_AnchorKey, m_source, m_menu, options);
    if(result.activated){
        m_command = result.key;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Testbed: custom context command key={}"), m_command);
    }
    const auto text = StringFormat(scratchArena, "Command: {} / Clicks: {}", m_command, m_anchorClicks);
    valid = ui.label(s_SelectedKey, { text.data(), text.size() }) && result.valid && valid;
    valid = ui.endPanel() && valid;
    ui.style().fontSize = previousFontSize;
    if(!valid)
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom popup tools declaration failed"));
}


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

