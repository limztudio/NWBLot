// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "ui_list_gallery.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool TestbedUiListSource::indexOf(const u64 keyValue, u64& index)const{
    if(keyValue == 0u || keyValue > rowCount())
        return false;
    index = keyValue - 1u;
    return true;
}

bool TestbedUiListSource::findEnabled(const u64 start, const bool reverse, u64& index)const{
    static_cast<void>(reverse);
    if(start >= rowCount())
        return false;
    index = start;
    return true;
}

StringView TestbedUiListSource::text(const u64 index)const{
    u64 remaining = key(index);
    if(remaining == 0u)
        return {};
    m_label[0] = 'R';
    m_label[1] = 'o';
    m_label[2] = 'w';
    m_label[3] = ' ';
    Array<char, 20u> digits{};
    usize count = 0u;
    do{
        digits[count] = static_cast<char>('0' + remaining % 10u);
        ++count;
        remaining /= 10u;
    }while(remaining != 0u);
    for(usize offset = 0u; offset < count; ++offset)
        m_label[4u + offset] = digits[count - 1u - offset];
    return { m_label.data(), 4u + count };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void TestbedUiListGallery::paint(NWB::Impl::UiPaintContext& context, const f32 x, const f32 y){
    using namespace NWB::Impl::Ui;
    Builder& ui = context.ui;
    if(!ui.beginPanel("list_gallery", { x, y, 280.0f, 258.0f }))
        return;
    const f32 previousFontSize = ui.style().fontSize;
    ui.style().fontSize = 14.0f;
    const WidgetOptions caption{ {}, { LayoutSizePolicy::Fixed, 20.0f } };
    bool valid = ui.label("title", "100000 virtual rows", caption);
    ListOptions options;
    options.height = { LayoutSizePolicy::Fixed, 180.0f };
    options.rowHeight = 24.0f;
    const ListResult result = ui.virtualList("rows", m_source, m_state, options);
    if(result.activated)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Testbed: custom list selected key={}"), m_state.selectedKey());
    NWB::Core::Alloc::ScratchArena scratchArena(Name("testbed/ui/list_caption"));
    const auto text = StringFormat(scratchArena, "Selected: {}", m_state.selectedKey());
    valid = ui.label("selected", { text.data(), text.size() }, caption) && result.valid && valid;
    valid = ui.endPanel() && valid;
    ui.style().fontSize = previousFontSize;
    if(!valid)
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom virtual list declaration failed"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

