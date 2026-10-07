// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "search_combo_source.h"

#include <global/assert.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u64 UiSearchComboSmokeSource::View::key(const u64 index)const{
    if(index >= rowCount())
        return 0u;
    u64 remaining = m_source.reversed() ? rowCount() - 1u - index : index;
    const u64 removed = m_source.removedKey();
    for(usize rangeIndex = 0u; rangeIndex < m_source.m_rangeCount; ++rangeIndex){
        const Range& range = m_source.m_ranges[rangeIndex];
        const bool excludes = removed >= range.first && removed < range.past;
        const u64 count = range.past - range.first - static_cast<u64>(excludes);
        if(remaining >= count){
            remaining -= count;
            continue;
        }
        u64 value = range.first + remaining;
        if(excludes && value >= removed)
            ++value;
        return value;
    }
    return 0u;
}

bool UiSearchComboSmokeSource::View::indexOf(const u64 keyValue, u64& index)const{
    if(keyValue == 0u || keyValue > 100000u || keyValue == m_source.removedKey())
        return false;
    u64 offset = 0u;
    const u64 removed = m_source.removedKey();
    for(usize rangeIndex = 0u; rangeIndex < m_source.m_rangeCount; ++rangeIndex){
        const Range& range = m_source.m_ranges[rangeIndex];
        const bool excludes = removed >= range.first && removed < range.past;
        if(keyValue >= range.first && keyValue < range.past){
            const u64 natural = offset + keyValue - range.first - static_cast<u64>(excludes && removed < keyValue);
            index = m_source.reversed() ? rowCount() - 1u - natural : natural;
            return true;
        }
        offset += range.past - range.first - static_cast<u64>(excludes);
    }
    return false;
}

bool UiSearchComboSmokeSource::View::findEnabled(const u64 start, const bool reverse, u64& index)const{
    if(start >= rowCount())
        return false;
    if(enabled(start)){
        index = start;
        return true;
    }
    if(reverse){
        if(start == 0u)
            return false;
        index = start - 1u;
    }
    else{
        if(start + 1u >= rowCount())
            return false;
        index = start + 1u;
    }
    return enabled(index);
}

StringView UiSearchComboSmokeSource::View::text(const u64 index)const{
    u64 fullIndex = 0u;
    if(!m_source.m_rows.indexOf(key(index), fullIndex))
        return {};
    return m_source.m_rows.text(fullIndex);
}

bool UiSearchComboSmokeSource::View::enabled(const u64 index)const{
    return index < rowCount() && key(index) != 5u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiSearchComboSmokeSource::UiSearchComboSmokeSource(Core::Alloc::GlobalArena& arena)
    : m_query(arena)
    , m_view(*this)
{
    const bool initialized = filter({});
    NWB_FATAL_ASSERT(initialized);
}

bool UiSearchComboSmokeSource::findEnabled(const u64 start, const bool reverse, u64& index)const{
    return m_rows.findEnabled(start, reverse, index);
}

bool UiSearchComboSmokeSource::filter(const AStringView query){
    if(query == AStringView(m_query.data(), m_query.size()) && m_cachedSourceRevision == revision())
        return true;
    if(query.empty())
        m_query.clear();
    else
        m_query.assign(query.data(), query.size());
    m_cachedSourceRevision = revision();
    m_rangeCount = 0u;
    m_filteredCount = 0u;
    if(query.empty())
        addRange(1u, 100001u);
    else{
        u64 prefix = 0u;
        bool valid = query.size() <= 6u && query.front() != '0';
        for(const char character : query){
            if(character < '0' || character > '9'){
                valid = false;
                break;
            }
            if(valid)
                prefix = prefix * 10u + static_cast<u64>(character - '0');
        }
        if(valid && prefix != 0u){
            u64 span = 1u;
            while(prefix <= 100000u){
                addRange(prefix, Min<u64>(100001u, prefix + span));
                prefix *= 10u;
                span *= 10u;
            }
        }
    }
    ++m_viewRevision;
    return true;
}

void UiSearchComboSmokeSource::addRange(const u64 first, const u64 past){
    if(first >= past || m_rangeCount >= m_ranges.size())
        return;
    m_ranges[m_rangeCount] = { first, past };
    ++m_rangeCount;
    m_filteredCount += past - first;
    if(removedKey() >= first && removedKey() < past)
        --m_filteredCount;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

