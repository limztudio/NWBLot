// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "model.h"
#include "grapheme.h"

#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool EditModel::beginComposition(){
    if(m_compositionActive)
        return true;
    m_compositionAnchor = m_anchor;
    m_compositionCaret = m_caret;
    m_preedit.clear();
    m_preeditAnchor = 0u;
    m_preeditCaret = 0u;
    m_compositionActive = true;
    advanceCompositionGeneration();
    return true;
}

bool EditModel::updateComposition(const AStringView value, const usize anchor, const usize caret){
    if(!m_compositionActive || value.size() > m_limits.maxBytes || !validateText(value))
        return false;
    if(!GraphemeSegmentation::isScalarBoundary(value, anchor) || !GraphemeSegmentation::isScalarBoundary(value, caret))
        return false;
    const usize retainedBytes = m_text.size() - (Max(m_compositionAnchor, m_compositionCaret)
        - Min(m_compositionAnchor, m_compositionCaret));
    if(retainedBytes > m_limits.maxBytes - value.size())
        return false;
    AString<Core::Alloc::GlobalArena> candidate(m_arena);
    if(!value.empty())
        candidate.assign(value.data(), value.size());
    m_preedit = Move(candidate);
    m_preeditAnchor = anchor;
    m_preeditCaret = caret;
    advanceCompositionGeneration();
    return true;
}

bool EditModel::commitComposition(const AStringView value){
    if(!m_compositionActive)
        return false;
    return replaceRange(Min(m_compositionAnchor, m_compositionCaret), Max(m_compositionAnchor, m_compositionCaret), value);
}

void EditModel::cancelComposition(){
    if(m_compositionActive)
        advanceCompositionGeneration();
    m_preedit.clear();
    m_preeditAnchor = 0u;
    m_preeditCaret = 0u;
    m_compositionAnchor = 0u;
    m_compositionCaret = 0u;
    m_compositionActive = false;
}

EditCompositionView EditModel::composition()const{
    return { { m_preedit.data(), m_preedit.size() }, m_preeditAnchor, m_preeditCaret,
        Min(m_compositionAnchor, m_compositionCaret), Max(m_compositionAnchor, m_compositionCaret), m_compositionActive };
}


void EditModel::advanceCompositionGeneration(){
    if(m_compositionGeneration == Limit<u64>::s_Max)
        TerminateInvariant();
    ++m_compositionGeneration;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

