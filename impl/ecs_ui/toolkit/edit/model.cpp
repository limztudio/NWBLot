// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "model.h"
#include "grapheme.h"
#include "multiline_text.h"

#include <global/atomic_identity.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_edit_model{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr usize s_MaxBytes = 1048576u;
static constexpr usize s_MaxHistoryRecords = 256u;
static constexpr usize s_MaxHistoryBytes = 16777216u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Atomic<u64> s_NextIdentity{ 1u };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static EditLimits BoundedLimits(const EditLimits& limits)noexcept{
    return { Min(limits.maxBytes, s_MaxBytes), Min(limits.maxHistoryRecords, s_MaxHistoryRecords),
        Min(limits.maxHistoryBytes, s_MaxHistoryBytes) };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


EditModel::EditModel(Core::Alloc::GlobalArena& arena, const EditLimits& limits, const EditTextMode::Enum mode)
    : m_arena(arena)
    , m_instanceGeneration(NextNonWrappingIdentity(__hidden_ui_edit_model::s_NextIdentity))
    , m_limits(__hidden_ui_edit_model::BoundedLimits(limits))
    , m_textMode(mode)
    , m_text(arena)
    , m_boundaries(arena)
    , m_history(arena)
    , m_preedit(arena)
{
    if(mode != EditTextMode::SingleLine && mode != EditTextMode::Multiline)
        TerminateInvariant();
    m_boundaries.push_back(0u);
}

bool EditModel::setText(const AStringView value){
    if(value.size() > m_limits.maxBytes)
        return false;
    auto boundaries = buildBoundaries(value);
    if(!boundaries)
        return false;
    AString<Core::Alloc::GlobalArena> candidate(m_arena);
    if(!value.empty())
        candidate.assign(value.data(), value.size());
    const bool changed = value != text();
    m_text = Move(candidate);
    m_boundaries = Move(*boundaries);
    m_anchor = m_text.size();
    m_caret = m_anchor;
    advanceSelectionGeneration();
    clearHistory();
    cancelComposition();
    if(changed)
        advanceRevision();
    if(m_externalRevision == Limit<u64>::s_Max)
        TerminateInvariant();
    ++m_externalRevision;
    return true;
}

bool EditModel::setSelection(const usize anchor, const usize caret)noexcept{
    if(!isBoundary(anchor) || !isBoundary(caret))
        return false;
    cancelComposition();
    m_anchor = anchor;
    m_caret = caret;
    advanceSelectionGeneration();
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
    const bool changed = after != text();
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

bool EditModel::validateText(const AStringView value)const{
    return m_textMode == EditTextMode::SingleLine ? GraphemeSegmentation::Validate(value, true) : ValidateMultilineText(value);
}

Expected<Vector<usize, Core::Alloc::GlobalArena>> EditModel::buildBoundaries(const AStringView value)const{
    if(m_textMode == EditTextMode::Multiline && !ValidateMultilineText(value))
        return MakeUnexpected(Failure{});
    return GraphemeSegmentation::Build(m_arena, value, m_textMode == EditTextMode::SingleLine);
}

void EditModel::advanceRevision()noexcept{
    if(m_revision != Limit<u64>::s_Max)
        ++m_revision;
}

void EditModel::advanceSelectionGeneration()noexcept{
    if(m_selectionGeneration == Limit<u64>::s_Max)
        TerminateInvariant();
    ++m_selectionGeneration;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

