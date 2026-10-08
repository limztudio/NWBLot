// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "model.h"
#include "grapheme.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool EditModel::eraseAroundSelection(const usize beforeBytes, const usize afterBytes){
    const usize start = selectionStart();
    const usize end = selectionEnd();
    if(beforeBytes > start || afterBytes > m_text.size() - end)
        return false;
    if(beforeBytes == 0u && afterBytes == 0u)
        return true;
    const usize prefixEnd = start - beforeBytes;
    const usize suffixStart = end + afterBytes;
    if(!isBoundary(prefixEnd) || !isBoundary(suffixStart))
        return false;

    AString<Core::Alloc::GlobalArena> candidate(m_arena);
    candidate.reserve(m_text.size() - beforeBytes - afterBytes);
    candidate.append(m_text.data(), prefixEnd);
    candidate.append(m_text.data() + start, end - start);
    candidate.append(m_text.data() + suffixStart, m_text.size() - suffixStart);
    const AStringView after(candidate.data(), candidate.size());
    auto boundaries = buildBoundaries(after);
    if(!boundaries)
        return false;
    const usize anchor = m_anchor - beforeBytes;
    const usize caret = m_caret - beforeBytes;
    const auto anchorBoundary = LowerBound(boundaries->begin(), boundaries->end(), anchor);
    const auto caretBoundary = LowerBound(boundaries->begin(), boundaries->end(), caret);
    if(
        anchorBoundary == boundaries->end() || *anchorBoundary != anchor
        || caretBoundary == boundaries->end() || *caretBoundary != caret
    )
        return false;

    recordHistory(text(), m_anchor, m_caret, after, anchor, caret);
    m_text = Move(candidate);
    m_boundaries = Move(*boundaries);
    m_anchor = anchor;
    m_caret = caret;
    advanceSelectionGeneration();
    cancelComposition();
    advanceRevision();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

