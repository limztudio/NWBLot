// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "model.h"
#include "grapheme.h"
#include "multiline_text.h"

#include <global/atomic_identity.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_edit_model{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Atomic<u64> s_NextIdentity{ 1u };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


EditModel::EditModel(Core::Alloc::GlobalArena& arena, const EditLimits& limits, const EditTextMode::Enum mode)
    : m_arena(arena)
    , m_instanceGeneration(NextNonWrappingIdentity(__hidden_ui_edit_model::s_NextIdentity))
    , m_limits(ClampEditLimits(limits))
    , m_text(arena)
    , m_boundaries(arena)
    , m_history(arena)
    , m_preedit(arena)
    , m_textMode(mode)
{
    if(mode != EditTextMode::SingleLine && mode != EditTextMode::Multiline)
        TerminateInvariant();
    m_boundaries.push_back(0u);
}

b8 EditModel::setText(const AStringView value){
    if(value.size() > m_limits.maxBytes)
        return false;
    auto boundaries = buildBoundaries(value);
    if(!boundaries)
        return false;
    AString<Core::Alloc::GlobalArena> candidate(m_arena);
    if(!value.empty())
        candidate.assign(value.data(), value.size());
    const b8 changed = value != text();
    m_text = Move(candidate);
    m_boundaries = Move(*boundaries);
    m_anchor = m_text.size();
    m_caret = m_anchor;
    advanceSelectionGeneration();
    clearHistory();
    cancelComposition();
    if(changed)
        advanceRevision();
    AdvanceRevision(m_externalRevision);
    return true;
}

b8 EditModel::setSelection(const usize anchor, const usize caret)noexcept{
    if(!isBoundary(anchor) || !isBoundary(caret))
        return false;
    cancelComposition();
    m_anchor = anchor;
    m_caret = caret;
    advanceSelectionGeneration();
    return true;
}

b8 EditModel::eraseSurrounding(const usize beforeBytes, const usize afterBytes){
    if(beforeBytes > m_caret || afterBytes > m_text.size() - m_caret)
        return false;
    if(beforeBytes == 0u && afterBytes == 0u)
        return true;
    return replaceRange(m_caret - beforeBytes, m_caret + afterBytes, {});
}

b8 EditModel::eraseAroundSelection(const usize beforeBytes, const usize afterBytes){
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
    if(anchorBoundary == boundaries->end() || *anchorBoundary != anchor || caretBoundary == boundaries->end() || *caretBoundary != caret)
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

b8 EditModel::undo(){
    if(!canUndo())
        return false;
    const HistoryRecord& record = m_history[m_historyCursor - 1u];
    return restoreHistoryState({ record.before.data(), record.before.size() }, record.beforeAnchor, record.beforeCaret, -1);
}

b8 EditModel::redo(){
    if(!canRedo())
        return false;
    const HistoryRecord& record = m_history[m_historyCursor];
    return restoreHistoryState({ record.after.data(), record.after.size() }, record.afterAnchor, record.afterCaret, 1);
}

b8 EditModel::beginComposition()noexcept{
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

b8 EditModel::updateComposition(const AStringView value, const usize anchor, const usize caret){
    if(!m_compositionActive || value.size() > m_limits.maxBytes || !validateText(value))
        return false;
    if(!IsUtf8ScalarBoundary(value, anchor) || !IsUtf8ScalarBoundary(value, caret))
        return false;
    const usize retainedBytes = m_text.size() - (Max(m_compositionAnchor, m_compositionCaret) - Min(m_compositionAnchor, m_compositionCaret));
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

b8 EditModel::commitComposition(const AStringView value){
    if(!m_compositionActive)
        return false;
    return replaceRange(Min(m_compositionAnchor, m_compositionCaret), Max(m_compositionAnchor, m_compositionCaret), value);
}

void EditModel::cancelComposition()noexcept{
    if(m_compositionActive)
        advanceCompositionGeneration();
    m_preedit.clear();
    m_preeditAnchor = 0u;
    m_preeditCaret = 0u;
    m_compositionAnchor = 0u;
    m_compositionCaret = 0u;
    m_compositionActive = false;
}

b8 EditModel::replaceRange(const usize begin, const usize end, const AStringView replacement){
    if(begin > end || !isBoundary(begin) || !isBoundary(end) || replacement.size() > m_limits.maxBytes)
        return false;
    const usize retainedBytes = m_text.size() - (end - begin);
    if(retainedBytes > m_limits.maxBytes - replacement.size() || !validateText(replacement))
        return false;
    AString<Core::Alloc::GlobalArena> candidate(m_arena);
    candidate.reserve(retainedBytes + replacement.size());
    candidate.append(m_text.data(), begin);
    if(!replacement.empty())
        candidate.append(replacement.data(), replacement.size());
    candidate.append(m_text.data() + end, m_text.size() - end);
    auto boundaries = buildBoundaries({ candidate.data(), candidate.size() });
    if(!boundaries)
        return false;
    // Inserted combining marks and neighboring emoji may merge clusters; keep the caret at a valid following edge.
    usize caret = begin + replacement.size();
    for(const usize boundary : *boundaries){
        if(boundary >= caret){
            caret = boundary;
            break;
        }
    }
    const AStringView after(candidate.data(), candidate.size());
    const b8 changed = after != text();
    if(changed)
        recordHistory(text(), m_anchor, m_caret, after, caret, caret);
    m_text = Move(candidate);
    m_boundaries = Move(*boundaries);
    m_anchor = caret;
    m_caret = caret;
    advanceSelectionGeneration();
    cancelComposition();
    if(changed)
        advanceRevision();
    return true;
}

b8 EditModel::validateText(const AStringView value)const noexcept{
    return m_textMode == EditTextMode::SingleLine ? GraphemeSegmentation::Validate(value, true) : ValidateMultilineText(value);
}

Expected<Vector<usize, Core::Alloc::GlobalArena>> EditModel::buildBoundaries(const AStringView value)const{
    if(m_textMode == EditTextMode::Multiline && !ValidateMultilineText(value))
        return MakeUnexpected(Failure{});
    return GraphemeSegmentation::Build(m_arena, value, m_textMode == EditTextMode::SingleLine);
}

b8 EditModel::restoreHistoryState(const AStringView value, const usize anchor, const usize caret, const isize cursorDelta){
    auto boundaries = buildBoundaries(value);
    if(!boundaries)
        return false;
    m_text.assign(value.data(), value.size());
    m_boundaries = Move(*boundaries);
    m_anchor = anchor;
    m_caret = caret;
    advanceSelectionGeneration();
    m_historyCursor = static_cast<usize>(static_cast<isize>(m_historyCursor) + cursorDelta);
    cancelComposition();
    advanceRevision();
    return true;
}

void EditModel::recordHistory(const AStringView before, const usize beforeAnchor, const usize beforeCaret, const AStringView after, const usize afterAnchor, const usize afterCaret){
    const usize cost = before.size() + after.size();
    if(m_limits.maxHistoryRecords == 0u || cost > m_limits.maxHistoryBytes){
        clearHistory();
        return;
    }
    HistoryRecord candidate(m_arena);
    if(!before.empty())
        candidate.before.assign(before.data(), before.size());
    if(!after.empty())
        candidate.after.assign(after.data(), after.size());
    candidate.beforeAnchor = beforeAnchor;
    candidate.beforeCaret = beforeCaret;
    candidate.afterAnchor = afterAnchor;
    candidate.afterCaret = afterCaret;
    while(m_history.size() > m_historyCursor){
        const HistoryRecord& discarded = m_history.back();
        m_historyBytes -= discarded.before.size() + discarded.after.size();
        m_history.pop_back();
    }
    while(!m_history.empty() && (m_history.size() >= m_limits.maxHistoryRecords || m_historyBytes > m_limits.maxHistoryBytes - cost)){
        const HistoryRecord& discarded = m_history.front();
        m_historyBytes -= discarded.before.size() + discarded.after.size();
        m_history.erase(m_history.begin());
        --m_historyCursor;
    }
    m_history.push_back(Move(candidate));
    m_historyBytes += cost;
    m_historyCursor = m_history.size();
}

void EditModel::clearHistory()noexcept{
    m_history.clear();
    m_historyCursor = 0u;
    m_historyBytes = 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

