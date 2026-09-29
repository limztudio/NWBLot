// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "model.h"
#include "grapheme.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_edit_model{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr usize s_MaxBytes = 1048576u;
static constexpr usize s_MaxHistoryRecords = 256u;
static constexpr usize s_MaxHistoryBytes = 16777216u;

static EditLimits BoundedLimits(const EditLimits& limits){
    return { Min(limits.maxBytes, s_MaxBytes), Min(limits.maxHistoryRecords, s_MaxHistoryRecords),
        Min(limits.maxHistoryBytes, s_MaxHistoryBytes) };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


EditModel::EditModel(Core::Alloc::GlobalArena& arena, const EditLimits& limits)
    : m_arena(arena)
    , m_limits(__hidden_ui_edit_model::BoundedLimits(limits))
    , m_text(arena)
    , m_boundaries(arena)
    , m_history(arena)
    , m_preedit(arena)
{
    m_boundaries.push_back(0u);
}

bool EditModel::setText(const AStringView value){
    if(value.size() > m_limits.maxBytes)
        return false;
    EditBoundaryVector boundaries(m_arena);
    if(!GraphemeSegmentation::Build(value, boundaries, true))
        return false;
    AString<Core::Alloc::GlobalArena> candidate(m_arena);
    if(!value.empty())
        candidate.assign(value.data(), value.size());
    const bool changed = value != text();
    m_text = Move(candidate);
    m_boundaries = Move(boundaries);
    m_anchor = m_text.size();
    m_caret = m_anchor;
    clearHistory();
    cancelComposition();
    if(changed)
        advanceRevision();
    return true;
}

bool EditModel::setSelection(const usize anchor, const usize caret){
    if(!isBoundary(anchor) || !isBoundary(caret))
        return false;
    cancelComposition();
    m_anchor = anchor;
    m_caret = caret;
    return true;
}

bool EditModel::selectAll(){
    return setSelection(0u, m_text.size());
}

bool EditModel::replaceSelection(const AStringView value){
    return replaceRange(selectionStart(), selectionEnd(), value);
}

bool EditModel::backspace(){
    const usize start = hasSelection() ? selectionStart() : previousBoundary(m_caret);
    const usize end = hasSelection() ? selectionEnd() : m_caret;
    return replaceRange(start, end, {});
}

bool EditModel::eraseForward(){
    const usize start = hasSelection() ? selectionStart() : m_caret;
    const usize end = hasSelection() ? selectionEnd() : nextBoundary(m_caret);
    return replaceRange(start, end, {});
}

bool EditModel::eraseSurrounding(const usize beforeBytes, const usize afterBytes){
    if(beforeBytes > m_caret || afterBytes > m_text.size() - m_caret)
        return false;
    if(beforeBytes == 0u && afterBytes == 0u)
        return true;
    return replaceRange(m_caret - beforeBytes, m_caret + afterBytes, {});
}

bool EditModel::replaceRange(const usize begin, const usize end, const AStringView replacement){
    if(begin > end || !isBoundary(begin) || !isBoundary(end) || replacement.size() > m_limits.maxBytes)
        return false;
    const usize retainedBytes = m_text.size() - (end - begin);
    if(retainedBytes > m_limits.maxBytes - replacement.size() || !GraphemeSegmentation::Validate(replacement, true))
        return false;
    AString<Core::Alloc::GlobalArena> candidate(m_arena);
    candidate.reserve(retainedBytes + replacement.size());
    candidate.append(m_text.data(), begin);
    if(!replacement.empty())
        candidate.append(replacement.data(), replacement.size());
    candidate.append(m_text.data() + end, m_text.size() - end);
    EditBoundaryVector boundaries(m_arena);
    if(!GraphemeSegmentation::Build({ candidate.data(), candidate.size() }, boundaries, true))
        return false;
    // Inserted combining marks and neighboring emoji may merge clusters; keep the caret at a valid following edge.
    usize caret = begin + replacement.size();
    for(const usize boundary : boundaries){
        if(boundary >= caret){
            caret = boundary;
            break;
        }
    }
    const AStringView after(candidate.data(), candidate.size());
    const bool changed = after != text();
    if(changed)
        recordHistory(text(), m_anchor, m_caret, after, caret, caret);
    m_text = Move(candidate);
    m_boundaries = Move(boundaries);
    m_anchor = caret;
    m_caret = caret;
    cancelComposition();
    if(changed)
        advanceRevision();
    return true;
}

void EditModel::advanceRevision(){
    if(m_revision != Limit<u64>::s_Max)
        ++m_revision;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

