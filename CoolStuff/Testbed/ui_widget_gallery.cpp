// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "ui_widget_gallery.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TestbedUiWidgetGallery::TestbedUiWidgetGallery(NWB::Core::Alloc::GlobalArena& arena)
    : m_edits(arena)
    , m_searchCombos(arena)
    , m_nestedPopups(arena)
    , m_numericEdits(arena)
    , m_textAreas(arena)
{}

void TestbedUiWidgetGallery::paint(NWB::Impl::UiPaintContext& context, const f32 x, const f32 y){
    using namespace NWB::Impl::Ui;
    Builder& ui = context.ui;
    if(!ui.beginPanel("interactive_gallery", { x, y, 280.0f, 180.0f }))
        return;
    bool valid = ui.label("title", "Interactive custom UI");
    if(ui.checkbox("enabled", "Enable counter", m_enabled))
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Testbed: custom UI counter enabled={}"), m_enabled);
    valid = ui.beginRow("actions") && valid;
    WidgetOptions options;
    options.enabled = m_enabled;
    options.width = { LayoutSizePolicy::Stretch, 1.0f };
    if(ui.button("increase", "Increase", options)){
        if(m_count != Limit<u32>::s_Max)
            ++m_count;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Testbed: custom UI counter={}"), m_count);
    }
    if(ui.button("reset", "Reset"))
        m_count = 0u;
    m_popups.trigger(ui);
    valid = ui.endContainer() && valid;
    NWB::Core::Alloc::ScratchArena scratchArena(Name("testbed/ui/widget_caption"));
    const auto caption = StringFormat(scratchArena, "Count: {}", m_count);
    valid = ui.label("count", { caption.data(), caption.size() }) && valid;
    valid = ui.label("keyboard_hint", "Tab / Shift+Tab, Enter / Space") && valid;
    valid = ui.endPanel() && valid;
    if(!valid)
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom UI widget declaration failed"));
    m_edits.paint(context, x, y + 192.0f);
    m_lists.paint(context, Max(18.0f, x - 292.0f), y);
    m_combos.paint(context, Max(18.0f, x - 292.0f), y + 270.0f);
    m_searchCombos.paint(context, Max(18.0f, x - 584.0f), y + 270.0f);
    m_popupTools.paint(context, Max(18.0f, x - 584.0f), y);
    m_nestedPopups.paint(context, Max(18.0f, x - 876.0f), y);
    m_numericEdits.paint(context, Max(18.0f, x - 876.0f), y + 270.0f);
    m_textAreas.paint(context, Max(18.0f, x - 584.0f), y + 490.0f);
    m_radioGroups.paint(context, Max(18.0f, x - 876.0f), y + 550.0f);
    m_sliders.paint(context, x, y + 550.0f);
    m_progress.paint(context, x, y + 358.0f);
    m_images.paint(context, Max(18.0f, x - 292.0f), y + 550.0f);
    m_popups.paint(context, x, y);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

