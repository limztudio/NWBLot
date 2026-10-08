// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "radio_group_source.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u64 UiRadioGroupSmokeSource::instanceGeneration()const{
    invalidateText();
    return m_generation;
}

u64 UiRadioGroupSmokeSource::revision()const{
    invalidateText();
    return m_revision;
}

u64 UiRadioGroupSmokeSource::rowCount()const{
    invalidateText();
    return m_removed == 0u ? 5u : 4u;
}

u64 UiRadioGroupSmokeSource::key(const u64 index)const{
    invalidateText();
    return rawKey(index);
}

Expected<u64> UiRadioGroupSmokeSource::indexOf(const u64 keyValue)const{
    u64 index = 0u;
    invalidateText();
    if(keyValue < 10u || keyValue > 50u || keyValue % 10u != 0u || keyValue == m_removed)
        return MakeUnexpected(Failure{});
    u64 forward = keyValue / 10u - 1u;
    if(m_removed != 0u && keyValue > m_removed)
        --forward;
    const u64 count = m_removed == 0u ? 5u : 4u;
    index = m_reversed ? count - 1u - forward : forward;
    return index < count ? Expected<u64>{ index } : MakeUnexpected(Failure{});
}

Expected<u64> UiRadioGroupSmokeSource::findEnabled(const u64 start, const bool reverseValue)const{
    u64 index = 0u;
    invalidateText();
    const u64 count = m_removed == 0u ? 5u : 4u;
    if(start >= count)
        return MakeUnexpected(Failure{});
    for(u64 cursor = start; cursor < count;){
        if(rawKey(cursor) != 30u){
            index = cursor;
            return index;
        }
        if(reverseValue){
            if(cursor == 0u)
                break;
            --cursor;
        }
        else
            ++cursor;
    }
    return MakeUnexpected(Failure{});
}

StringView UiRadioGroupSmokeSource::text(const u64 index)const{
    invalidateText();
    ++m_labelReads;
    constexpr StringView s_Labels[]{ "Alpha", "Beta", "Gamma (disabled)", "Delta", "Epsilon" };
    const u64 choice = rawKey(index);
    if(choice == 0u)
        return {};
    const StringView label = s_Labels[choice / 10u - 1u];
    NWB_MEMCPY(m_label.data(), m_label.size(), label.data(), label.size());
    return { m_label.data(), label.size() };
}

bool UiRadioGroupSmokeSource::enabled(const u64 index)const{
    invalidateText();
    const u64 choice = rawKey(index);
    return choice != 0u && choice != 30u;
}

void UiRadioGroupSmokeSource::reverse()noexcept{
    m_reversed = !m_reversed;
    ++m_revision;
}

void UiRadioGroupSmokeSource::remove(const u64 keyValue)noexcept{
    if(keyValue < 10u || keyValue > 50u || keyValue % 10u != 0u || m_removed != 0u)
        return;
    m_removed = keyValue;
    ++m_revision;
}

void UiRadioGroupSmokeSource::replace()noexcept{
    ++m_generation;
    ++m_revision;
    m_removed = 0u;
    m_reversed = false;
}

void UiRadioGroupSmokeSource::beginFrame()noexcept{
    m_labelReads = 0u;
}

u64 UiRadioGroupSmokeSource::rawKey(const u64 index)const{
    const u64 count = m_removed == 0u ? 5u : 4u;
    if(index >= count)
        return 0u;
    const u64 forward = m_reversed ? count - 1u - index : index;
    u64 choice = (forward + 1u) * 10u;
    if(m_removed != 0u && choice >= m_removed)
        choice += 10u;
    return choice;
}

void UiRadioGroupSmokeSource::invalidateText()const{
    m_label.fill('?');
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

