// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "list_source.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u64 UiListSmokeSource::key(const u64 index)const{
    const u64 count = rowCount();
    if(index >= count)
        return 0u;
    u64 value = (m_reversed ? count - 1u - index : index) + 1u;
    if(m_removed != 0u && value >= m_removed)
        ++value;
    return value;
}

bool UiListSmokeSource::indexOf(const u64 keyValue, u64& index)const{
    if(keyValue == 0u || keyValue > 100000u || keyValue == m_removed)
        return false;
    const u64 natural = keyValue - 1u - static_cast<u64>(m_removed != 0u && m_removed < keyValue);
    index = m_reversed ? rowCount() - 1u - natural : natural;
    return true;
}

bool UiListSmokeSource::findEnabled(const u64 start, const bool reverse, u64& index)const{
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

StringView UiListSmokeSource::text(const u64 index)const{
    ++m_labelReads;
    const u64 rowKey = key(index);
    if(rowKey == 0u)
        return {};
    m_label[0] = 'R';
    m_label[1] = 'o';
    m_label[2] = 'w';
    m_label[3] = ' ';
    Array<char, 20u> digits{};
    u64 remaining = rowKey;
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

bool UiListSmokeSource::enabled(const u64 index)const{
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

