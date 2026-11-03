// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "ui_widget_gallery.h"
#include "CoolStuff/Testbed/ui_gallery_constants.adhd.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::Impl::Ui::Rect UiWidgetGallery::LayoutBounds(const NWB::Impl::Ui::DisplayMetrics& display, const f32 preferredTop)noexcept{
    return ResolveWidgetGalleryLayoutBounds(display, preferredTop);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void UiWidgetGallery::paint(NWB::Impl::UiPaintContext& context, const NWB::Impl::Ui::Rect& bounds){
    using namespace NWB::Impl::Ui;
    Builder& ui = context.ui;
    const u32 previous = m_selectedGallery;
    if(!ui.beginPanel("gallery_selector", { bounds.x, bounds.y, bounds.width, s_SelectorHeight }))
        return;
    ContainerOptions row;
    b8 valid = ui.beginRow("navigation", row);
    WidgetOptions navigation;
    navigation.width = { LayoutSizePolicy::Fixed, s_NavigationButtonWidth };
    navigation.enabled = m_selectedGallery != 0u && !ui.input().hasPopup();
    if(ui.button("previous", "Previous", navigation))
        --m_selectedGallery;
    navigation.enabled = m_selectedGallery + 1u < Section::kCount && !ui.input().hasPopup();
    if(ui.button("next", "Next", navigation))
        ++m_selectedGallery;
    valid = ui.endContainer() && valid;
    const u32 selected = m_selectedGallery;
    NWB::Core::Alloc::ScratchArena scratchArena(s_SelectorArena);
    const auto caption = StringFormat(scratchArena, "UI samples {}/{}: {}", selected + 1u, static_cast<u32>(Section::kCount), SectionName(static_cast<Section::Enum>(selected)));
    valid = ui.label("section", { caption.data(), caption.size() }) && valid;
    valid = ui.endPanel() && valid;
    if(!valid){
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: UI gallery selector declaration failed"));
        return;
    }
    if(previous != selected)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Testbed: UI gallery section={}"), m_selectedGallery + 1u);

    const f32 width = ResolveWidgetGalleryContentWidth(selected);
    const f32 x = bounds.x + (bounds.width - width) * 0.5f;
    const f32 y = bounds.y + s_SelectorHeight + s_ContentGap;
    switch(selected){
    case Section::Controls:
        paintControls(context, scratchArena, x, y);
        m_popups.paint(context, x, y);
        break;
    case Section::Edit: m_edits.paint(context, x, y); break;
    case Section::List: m_lists.paint(context, scratchArena, x, y); break;
    case Section::Combo: m_combos.paint(context, scratchArena, x, y); break;
    case Section::SearchCombo: m_searchCombos.paint(context, scratchArena, x, y); break;
    case Section::PopupTools: m_popupTools.paint(context, scratchArena, x, y); break;
    case Section::NestedPopup: m_nestedPopups.paint(context, x, y); break;
    case Section::NumericEdit: m_numericEdits.paint(context, scratchArena, x, y); break;
    case Section::TextArea: m_textAreas.paint(context, x, y); break;
    case Section::RadioGroup: m_radioGroups.paint(context, x, y); break;
    case Section::Slider: m_sliders.paint(context, x, y); break;
    case Section::Progress: m_progress.paint(context, scratchArena, x, y); break;
    case Section::Image: m_images.paint(context, x, y); break;
    default: break;
    }
}

void UiWidgetGallery::paintControls(NWB::Impl::UiPaintContext& context, NWB::Core::Alloc::ScratchArena& scratchArena, const f32 x, const f32 y){
    using namespace NWB::Impl::Ui;
    Builder& ui = context.ui;
    if(!ui.beginPanel("interactive_gallery", { x, y, s_OrdinaryGalleryWidth, s_ControlsPanelHeight }))
        return;
    b8 valid = ui.label(s_TitleKey, "Interactive custom UI");
    if(ui.checkbox(s_EnabledKey, "Enable counter", m_enabled))
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
    const auto caption = StringFormat(scratchArena, "Count: {}", m_count);
    valid = ui.label("count", { caption.data(), caption.size() }) && valid;
    valid = ui.label("keyboard_hint", "Tab / Shift+Tab, Enter / Space") && valid;
    valid = ui.endPanel() && valid;
    if(!valid)
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom UI widget declaration failed"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

