// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "ui_list_gallery.h"
#include "ui_gallery_constants.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<u64> UiListSource::indexOf(const u64 keyValue)const noexcept{
    if(keyValue == 0u || keyValue > rowCount())
        return MakeUnexpected(Failure{});
    return keyValue - 1u;
}

Expected<u64> UiListSource::findEnabled(const u64 start, const bool reverse)const noexcept{
    static_cast<void>(reverse);
    if(start >= rowCount())
        return MakeUnexpected(Failure{});
    return start;
}

StringView UiListSource::text(const u64 index)const noexcept{
    constexpr usize s_MaxDecimalDigits = 20u;
    constexpr u64 s_DecimalRadix = 10u;
    constexpr usize s_RowPrefixLength = 4u;
    u64 remaining = key(index);
    if(remaining == 0u)
        return {};
    m_label[0] = 'R';
    m_label[1] = 'o';
    m_label[2] = 'w';
    m_label[3] = ' ';
    Array<char, s_MaxDecimalDigits> digits{};
    usize count = 0u;
    do{
        digits[count] = static_cast<char>('0' + remaining % s_DecimalRadix);
        ++count;
        remaining /= s_DecimalRadix;
    }while(remaining != 0u);
    for(usize offset = 0u; offset < count; ++offset)
        m_label[s_RowPrefixLength + offset] = digits[count - 1u - offset];
    return { m_label.data(), s_RowPrefixLength + count };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void UiListGallery::paint(NWB::Impl::UiPaintContext& context, NWB::Core::Alloc::ScratchArena& scratchArena, const f32 x, const f32 y){
    using namespace NWB::Impl::Ui;
    constexpr f32 s_PanelHeight = 258.0f;
    constexpr f32 s_ListHeight = 180.0f;
    Builder& ui = context.ui;
    if(!ui.beginPanel("list_gallery", { x, y, s_OrdinaryGalleryWidth, s_PanelHeight }))
        return;
    const f32 previousFontSize = ui.style().fontSize;
    ui.style().fontSize = s_GalleryFontSize;
    bool valid = ui.label(s_TitleKey, "100000 virtual rows");
    ListOptions options;
    options.height = { LayoutSizePolicy::Fixed, s_ListHeight };
    const ListResult result = ui.virtualList("rows", m_source, m_state, options);
    if(result.activated)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Testbed: custom list selected key={}"), m_state.selectedKey());
    const auto text = StringFormat(scratchArena, "Selected: {}", m_state.selectedKey());
    valid = ui.label(s_SelectedKey,{ text.data(), text.size() }) && result.valid && valid;
    valid = ui.endPanel() && valid;
    ui.style().fontSize = previousFontSize;
    if(!valid)
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom virtual list declaration failed"));
}


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

