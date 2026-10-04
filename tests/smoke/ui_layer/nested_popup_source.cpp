// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "nested_popup_source.h"

#include <global/assert.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_nested_popup_source{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr Array<StringView, 5u> s_Labels{ "Alpha", "Beta", "Gamma (disabled)", "Delta", "Echo" };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static char Lower(const char value){
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
}

[[nodiscard]] static bool Matches(const StringView text, const AStringView query){
    if(query.size() > text.size())
        return false;
    for(usize start = 0u; start <= text.size() - query.size(); ++start){
        bool matched = true;
        for(usize offset = 0u; offset < query.size(); ++offset){
            if(Lower(text[start + offset]) != Lower(query[offset])){
                matched = false;
                break;
            }
        }
        if(matched)
            return true;
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u64 UiNestedPopupSmokeSource::View::key(const u64 index)const{
    return index < rowCount() ? m_source.key(m_source.m_indices[index]) : 0u;
}

bool UiNestedPopupSmokeSource::View::indexOf(const u64 keyValue, u64& index)const{
    for(u64 candidate = 0u; candidate < rowCount(); ++candidate){
        if(key(candidate) == keyValue){
            index = candidate;
            return true;
        }
    }
    return false;
}

bool UiNestedPopupSmokeSource::View::findEnabled(const u64 start, const bool reverse, u64& index)const{
    if(start >= rowCount())
        return false;
    for(u64 candidate = start; candidate < rowCount(); reverse ? --candidate : ++candidate){
        if(enabled(candidate)){
            index = candidate;
            return true;
        }
        if(reverse && candidate == 0u)
            break;
    }
    return false;
}

StringView UiNestedPopupSmokeSource::View::text(const u64 index)const{
    return index < rowCount() ? m_source.text(m_source.m_indices[index]) : StringView{};
}

bool UiNestedPopupSmokeSource::View::enabled(const u64 index)const{
    return index < rowCount() && m_source.enabled(m_source.m_indices[index]);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiNestedPopupSmokeSource::UiNestedPopupSmokeSource(Core::Alloc::GlobalArena& arena)
    : m_query(arena)
    , m_view(*this)
{
    const bool initialized = filter({});
    GLB_FATAL_ASSERT(initialized);
}

bool UiNestedPopupSmokeSource::indexOf(const u64 keyValue, u64& index)const{
    if(keyValue == 0u || keyValue > rowCount())
        return false;
    index = keyValue - 1u;
    return true;
}

bool UiNestedPopupSmokeSource::findEnabled(const u64 start, const bool reverse, u64& index)const{
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

StringView UiNestedPopupSmokeSource::text(const u64 index)const{
    ++m_labelReads;
    return index < rowCount() ? __hidden_ui_nested_popup_source::s_Labels[index] : StringView{};
}

bool UiNestedPopupSmokeSource::filter(const AStringView query){
    if(m_viewRevision != 0u && query == AStringView(m_query.data(), m_query.size()))
        return true;
    if(query.empty())
        m_query.clear();
    else
        m_query.assign(query.data(), query.size());
    m_count = 0u;
    for(u64 index = 0u; index < rowCount(); ++index){
        if(!__hidden_ui_nested_popup_source::Matches(__hidden_ui_nested_popup_source::s_Labels[index], query))
            continue;
        m_indices[m_count] = index;
        ++m_count;
    }
    ++m_viewRevision;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

