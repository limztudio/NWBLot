// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "list_source.h"
#include "smoke_geometry.h"

#include <tests/unit/ui/arithmetic_fixture_rows.h>

#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u64 UiListSmokeSource::key(const u64 index)const noexcept{
    const u64 count = rowCount();
    return index < count ? UiWidgetTests::ArithmeticFixtureRowKey(count, m_removed, m_reversed, index) : 0u;
}

Expected<u64> UiListSmokeSource::indexOf(const u64 keyValue)const noexcept{
    return UiWidgetTests::FindArithmeticFixtureRowIndex(rowCount(), m_removed, m_reversed, keyValue);
}

Expected<u64> UiListSmokeSource::findEnabled(const u64 start, const b8 reverse)const noexcept{
    return FindAdjacentEnabledRow(*this, start, reverse);
}

StringView UiListSmokeSource::text(const u64 index)const noexcept{
    ++m_labelReads;
    const u64 rowKey = key(index);
    if(rowKey == 0u)
        return {};
    m_label[0] = 'R';
    m_label[1] = 'o';
    m_label[2] = 'w';
    m_label[3] = ' ';
    Array<char, s_UsizeDecimalDigitCount> digits{};
    const AStringView formatted = FormatDecimal(rowKey, digits);
    NWB_MEMCPY(m_label.data() + 4u, m_label.size() - 4u, formatted.data(), formatted.size());
    return { m_label.data(), 4u + formatted.size() };
}

b8 UiListSmokeSource::enabled(const u64 index)const noexcept{
    return index < rowCount() && key(index) != 5u;
}

void UiListSmokeSource::reverse()noexcept{
    m_reversed = !m_reversed;
    ++m_revision;
}

void UiListSmokeSource::remove(const u64 keyValue)noexcept{
    if(keyValue == 0u || keyValue > 100000u || keyValue == m_removed)
        return;
    m_removed = keyValue;
    ++m_revision;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

