// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "ui_list_gallery.h"
#include "ui_gallery_support.h"

#include <core/common/log.h>

#include <global/numeric_text.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<u64> UiListSource::indexOf(const u64 keyValue)const noexcept{
    if(keyValue == 0u || keyValue > rowCount())
        return MakeUnexpected(Failure{});
    return keyValue - 1u;
}

Expected<u64> UiListSource::findEnabled(const u64 start, [[maybe_unused]] const b8 reverse)const noexcept{
    if(start >= rowCount())
        return MakeUnexpected(Failure{});
    return start;
}

StringView UiListSource::text(const u64 index)const noexcept{
    const u64 rowKey = key(index);
    if(rowKey == 0u)
        return {};
    m_label[0] = 'R';
    m_label[1] = 'o';
    m_label[2] = 'w';
    m_label[3] = ' ';
    Array<char, s_VirtualListMaxDecimalDigits> digits{};
    const AStringView formatted = FormatDecimal(static_cast<usize>(rowKey), digits);
    for(usize offset = 0u; offset < formatted.size(); ++offset)
        m_label[s_VirtualListRowPrefixLength + offset] = formatted[offset];
    return { m_label.data(), s_VirtualListRowPrefixLength + formatted.size() };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void UiListGallery::paint(NWB::Impl::UiPaintContext& context, NWB::Core::Alloc::ScratchArena& scratchArena, const f32 x, const f32 y){
    using namespace NWB::Impl::Ui;
    Builder& ui = context.ui;
    if(!ui.beginPanel("list_gallery", { x, y, s_OrdinaryGalleryWidth, s_ListGalleryPanelHeight }))
        return;
    const b8 valid = WithGalleryFontSize(ui, [&](){
        b8 declared = ui.label(s_TitleKey, "100000 virtual rows");
        ListOptions options;
        options.height = { LayoutSizePolicy::Fixed, s_ListGalleryListHeight };
        const ListResult result = ui.virtualList("rows", m_source, m_state, options);
        if(result.activated)
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Testbed: custom list selected key={}"), m_state.selectedKey());
        const auto text = StringFormat(scratchArena, s_SelectedTextFormat, m_state.selectedKey());
        declared = ui.label(s_SelectedKey, { text.data(), text.size() }) && result.valid && declared;
        return ui.endPanel() && declared;
    });
    if(!valid)
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom virtual list declaration failed"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

