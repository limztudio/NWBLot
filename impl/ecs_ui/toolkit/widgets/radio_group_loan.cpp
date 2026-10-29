// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../builder.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 Builder::radioGroupStateMatches(const RadioGroupFrame& frame)const noexcept{
    return popupAncestorsVisible() && frame.m_state.matches(frame.m_snapshot);
}

b8 Builder::radioGroupMatches(const RadioGroupFrame& frame)const{
    constexpr u32 s_StampFencePassCount = 2u;
    // Repeat the stamp fence to catch a one-shot metadata mutation from the final count callback.
    for(u32 pass = 0u; pass < s_StampFencePassCount; ++pass){
        if(
            !radioGroupStateMatches(frame)
            || frame.m_source.instanceGeneration() != frame.m_choices.sourceGeneration || !radioGroupStateMatches(frame)
            || frame.m_source.revision() != frame.m_choices.sourceRevision || !radioGroupStateMatches(frame)
            || frame.m_source.rowCount() != frame.m_choices.count || !radioGroupStateMatches(frame)
        )
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

