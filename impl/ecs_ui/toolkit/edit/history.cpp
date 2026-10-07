// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "model.h"
#include "grapheme.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool EditModel::undo(){
    if(!canUndo())
        return false;
    const HistoryRecord& record = m_history[m_historyCursor - 1u];
    EditBoundaryVector boundaries(m_arena);
    const AStringView value(record.before.data(), record.before.size());
    if(!buildBoundaries(value, boundaries))
        return false;
    AString<Core::Alloc::GlobalArena> candidate(record.before, m_arena);
    m_text = Move(candidate);
    m_boundaries = Move(boundaries);
    m_anchor = record.beforeAnchor;
    m_caret = record.beforeCaret;
    advanceSelectionGeneration();
    --m_historyCursor;
    cancelComposition();
    advanceRevision();
    return true;
}

bool EditModel::redo(){
    if(!canRedo())
        return false;
    const HistoryRecord& record = m_history[m_historyCursor];
    EditBoundaryVector boundaries(m_arena);
    const AStringView value(record.after.data(), record.after.size());
    if(!buildBoundaries(value, boundaries))
        return false;
    AString<Core::Alloc::GlobalArena> candidate(record.after, m_arena);
    m_text = Move(candidate);
    m_boundaries = Move(boundaries);
    m_anchor = record.afterAnchor;
    m_caret = record.afterCaret;
    advanceSelectionGeneration();
    ++m_historyCursor;
    cancelComposition();
    advanceRevision();
    return true;
}

void EditModel::recordHistory(const AStringView before, const usize beforeAnchor, const usize beforeCaret,
    const AStringView after, const usize afterAnchor, const usize afterCaret
){
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
    while(
        !m_history.empty() && (m_history.size() >= m_limits.maxHistoryRecords
            || m_historyBytes > m_limits.maxHistoryBytes - cost)
    ){
        const HistoryRecord& discarded = m_history.front();
        m_historyBytes -= discarded.before.size() + discarded.after.size();
        m_history.erase(m_history.begin());
        --m_historyCursor;
    }
    m_history.push_back(Move(candidate));
    m_historyBytes += cost;
    m_historyCursor = m_history.size();
}

void EditModel::clearHistory(){
    m_history.clear();
    m_historyCursor = 0u;
    m_historyBytes = 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

