// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "ui_widget_gallery.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>

#include <global/math/vector_arithmetic.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_testbed_ui_gallery{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Section{
    enum Enum : u32{
        Controls, Edit, List, Combo, SearchCombo, PopupTools, NestedPopup,
        NumericEdit, TextArea, RadioGroup, Slider, Progress, Image, kCount
    };
};

static constexpr f32 s_Margin = 18.0f;
static constexpr f32 s_PreferredTop = 246.0f;
static constexpr f32 s_SelectorWidth = 400.0f;
static constexpr f32 s_SelectorHeight = 88.0f;
static constexpr f32 s_ContentHeight = 260.0f;
static constexpr f32 s_ContentGap = 12.0f;
static constexpr f32 s_OrdinaryGalleryWidth = 280.0f;
static constexpr Name s_SelectorArena("testbed/ui/gallery_selector");
static constexpr Array<StringView, Section::kCount> s_SectionNames{
    "Controls and menu", "Text editing", "Virtual list", "Keyed combo", "Searchable combo",
    "Tooltips and context menu", "Nested popups", "Numeric editing", "Multiline text",
    "Radio choices", "Slider", "Progress", "Images"
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::Impl::Ui::Rect UiWidgetGallery::LayoutBounds(const NWB::Impl::Ui::DisplayMetrics& display){
    using namespace __hidden_testbed_ui_gallery;
    const f32 height = s_SelectorHeight + s_ContentGap + s_ContentHeight;
    const SIMDVector available = VectorSubtract(
        VectorSubtract(VectorSet(display.logicalWidth, display.logicalHeight, 0.0f, 0.0f), VectorSet(s_SelectorWidth, height, 0.0f, 0.0f)),
        VectorReplicate(s_Margin)
    );
    const SIMDVector preferredTop = VectorSet(0.0f, s_PreferredTop, 0.0f, 0.0f);
    const SIMDVector topLimit = VectorAndInt(VectorLess(preferredTop, available), VectorSelectControl(0u, 1u, 0u, 0u));
    const SIMDVector capped = VectorSelect(available, preferredTop, topLimit);
    const SIMDVector margin = VectorReplicate(s_Margin);
    const SIMDVector origin = VectorSelect(capped, margin, VectorGreater(margin, capped));
    return { VectorGetX(origin), VectorGetY(origin), s_SelectorWidth, height };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiWidgetGallery::UiWidgetGallery(
    NWB::Core::Alloc::GlobalArena& arena, const NWB::Core::Assets::AssetManager& assets
)
    : m_edits(arena)
    , m_searchCombos(arena)
    , m_nestedPopups(arena)
    , m_numericEdits(arena)
    , m_textAreas(arena)
    , m_images(arena, assets)
{}

void UiWidgetGallery::paint(NWB::Impl::UiPaintContext& context){
    using namespace NWB::Impl::Ui;
    using namespace __hidden_testbed_ui_gallery;
    Builder& ui = context.ui;
    const Rect bounds = LayoutBounds(context.display);
    const u32 previous = m_selectedGallery;
    if(!ui.beginPanel("gallery_selector", { bounds.x, bounds.y, bounds.width, s_SelectorHeight }))
        return;
    ContainerOptions row;
    row.height = { LayoutSizePolicy::Fixed, 32.0f };
    bool valid = ui.beginRow("navigation", row);
    WidgetOptions navigation;
    navigation.width = { LayoutSizePolicy::Fixed, 100.0f };
    navigation.height = { LayoutSizePolicy::Fixed, 32.0f };
    navigation.enabled = m_selectedGallery != 0u && !ui.input().hasPopup();
    if(ui.button("previous", "Previous", navigation))
        --m_selectedGallery;
    navigation.enabled = m_selectedGallery + 1u < Section::kCount && !ui.input().hasPopup();
    if(ui.button("next", "Next", navigation))
        ++m_selectedGallery;
    valid = ui.endContainer() && valid;
    const u32 selected = m_selectedGallery;
    NWB::Core::Alloc::ScratchArena scratchArena(s_SelectorArena);
    const auto caption = StringFormat(
        scratchArena,
        "UI samples {}/{}: {}",
        selected + 1u,
        static_cast<u32>(Section::kCount),
        s_SectionNames[selected]
    );
    valid = ui.label("section", { caption.data(), caption.size() }) && valid;
    valid = ui.endPanel() && valid;
    if(!valid){
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: UI gallery selector declaration failed"));
        return;
    }
    if(previous != selected)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Testbed: UI gallery section={}"), m_selectedGallery + 1u);

    const f32 width = selected == Section::TextArea ? s_SelectorWidth : s_OrdinaryGalleryWidth;
    const SIMDVector centeredOrigin = VectorMultiplyAddExpression(
        VectorSubtract(VectorSet(bounds.width, s_SelectorHeight, 0.0f, 0.0f), VectorSet(width, 0.0f, 0.0f, 0.0f)),
        VectorSet(0.5f, 1.0f, 0.0f, 0.0f),
        VectorSet(bounds.x, bounds.y, 0.0f, 0.0f)
    );
    const SIMDVector contentOrigin = VectorAdd(centeredOrigin, VectorSet(0.0f, s_ContentGap, 0.0f, 0.0f));
    const f32 x = VectorGetX(centeredOrigin);
    const f32 y = VectorGetY(contentOrigin);
    switch(selected){
    case Section::Controls:
        paintControls(context, x, y);
        m_popups.paint(context, x, y);
        break;
    case Section::Edit: m_edits.paint(context, x, y); break;
    case Section::List: m_lists.paint(context, x, y); break;
    case Section::Combo: m_combos.paint(context, x, y); break;
    case Section::SearchCombo: m_searchCombos.paint(context, x, y); break;
    case Section::PopupTools: m_popupTools.paint(context, x, y); break;
    case Section::NestedPopup: m_nestedPopups.paint(context, x, y); break;
    case Section::NumericEdit: m_numericEdits.paint(context, x, y); break;
    case Section::TextArea: m_textAreas.paint(context, x, y); break;
    case Section::RadioGroup: m_radioGroups.paint(context, x, y); break;
    case Section::Slider: m_sliders.paint(context, x, y); break;
    case Section::Progress: m_progress.paint(context, x, y); break;
    case Section::Image: m_images.paint(context, x, y); break;
    default: break;
    }
}

void UiWidgetGallery::paintControls(NWB::Impl::UiPaintContext& context, const f32 x, const f32 y){
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
}


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

