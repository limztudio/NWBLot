// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "grapheme.h"

#include <impl/ecs_ui/toolkit/global.h>

#include <impl/ecs_ui/toolkit/edit/model_validation.adhd.h>

#include <global/atomic_identity.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// UTF8 byte selections always lie on Unicode 17 extended grapheme boundaries; preedit stays separate from committed text.
class EditModel final : NoCopy{
private:
    using HistoryRecord = EditHistoryRecord;


private:
    [[nodiscard]] static u64 NextIdentity()noexcept{
        static Atomic<u64> s_NextIdentity{ 1u };
        return NextNonWrappingIdentity(s_NextIdentity);
    }


public:
    explicit EditModel(Core::Alloc::GlobalArena& arena, const EditLimits& limits = {}, const EditTextMode::Enum mode = EditTextMode::SingleLine)
        : m_arena(arena)
        , m_instanceGeneration(NextIdentity())
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


public:
    [[nodiscard]] AStringView text()const noexcept{ return { m_text.data(), m_text.size() }; }

    [[nodiscard]] u64 instanceGeneration()const noexcept{ return m_instanceGeneration; }

    [[nodiscard]] u64 externalRevision()const noexcept{ return m_externalRevision; }

    [[nodiscard]] u64 revision()const noexcept{ return m_revision; }

    [[nodiscard]] u64 compositionGeneration()const noexcept{ return m_compositionGeneration; }

    [[nodiscard]] u64 selectionGeneration()const noexcept{ return m_selectionGeneration; }

    [[nodiscard]] usize anchor()const noexcept{ return m_anchor; }

    [[nodiscard]] usize caret()const noexcept{ return m_caret; }

    [[nodiscard]] usize selectionStart()const noexcept{ return Min(m_anchor, m_caret); }

    [[nodiscard]] usize selectionEnd()const noexcept{ return Max(m_anchor, m_caret); }

    [[nodiscard]] b8 hasSelection()const noexcept{ return m_anchor != m_caret; }

    [[nodiscard]] AStringView selectedText()const noexcept{ return text().substr(selectionStart(), selectionEnd() - selectionStart()); }

    [[nodiscard]] const Vector<usize, Core::Alloc::GlobalArena>& graphemeBoundaries()const noexcept{ return m_boundaries; }

    [[nodiscard]] const EditLimits& limits()const noexcept{ return m_limits; }

    [[nodiscard]] EditTextMode::Enum textMode()const noexcept{ return m_textMode; }

    [[nodiscard]] TextAreaModelStamp stamp()const noexcept{
        return { m_instanceGeneration, m_revision, m_externalRevision, m_selectionGeneration, m_compositionGeneration, m_anchor, m_caret, m_textMode };
    }

    [[nodiscard]] b8 matchesStamp(const TextAreaModelStamp& expected)const noexcept{
        return stamp() == expected;
    }

    [[nodiscard]] b8 setText(const AStringView value){
        if(value.size() > m_limits.maxBytes)
            return false;
        auto boundaries = buildBoundaries(value);
        if(!boundaries)
            return false;
        CommitEditModelSetText(
            value,
            *boundaries,
            m_text,
            m_boundaries,
            m_anchor,
            m_caret,
            m_selectionGeneration,
            m_history,
            m_historyCursor,
            m_historyBytes,
            m_preedit,
            m_preeditAnchor,
            m_preeditCaret,
            m_compositionAnchor,
            m_compositionCaret,
            m_compositionActive,
            m_compositionGeneration,
            m_revision,
            m_externalRevision
        );
        return true;
    }

    [[nodiscard]] b8 setSelection(const usize anchor, const usize caret)noexcept{
        return ApplyEditModelSetSelection(
            m_boundaries,
            anchor,
            caret,
            m_anchor,
            m_caret,
            m_selectionGeneration,
            m_preedit,
            m_preeditAnchor,
            m_preeditCaret,
            m_compositionAnchor,
            m_compositionCaret,
            m_compositionActive,
            m_compositionGeneration
        );
    }

    [[nodiscard]] b8 selectAll()noexcept{ return setSelection(0u, m_text.size()); }

    // Home/End address the current hard line in Multiline mode; document movement always addresses the complete text.
    // Word movement groups whitespace, ASCII punctuation, and all remaining graphemes without a linguistic word claim.
    [[nodiscard]] b8 move(const EditMove::Enum movement, const b8 extend = false)noexcept{
        return ApplyEditModelMove(
            text(),
            m_boundaries,
            m_textMode,
            movement,
            extend,
            m_anchor,
            m_caret,
            m_selectionGeneration,
            m_preedit,
            m_preeditAnchor,
            m_preeditCaret,
            m_compositionAnchor,
            m_compositionCaret,
            m_compositionActive,
            m_compositionGeneration
        );
    }

    // Returns the complete run containing a grapheme boundary; document end selects the preceding run.
    [[nodiscard]] Expected<EditWordRange> wordRangeAt(const usize position)const noexcept{
        return ResolveEditWordRangeAt(text(), m_boundaries, position);
    }

    [[nodiscard]] b8 replaceSelection(const AStringView value){ return replaceRange(selectionStart(), selectionEnd(), value); }

    [[nodiscard]] b8 backspace(){
        const EditWordRange range = ResolveEditBackspaceRange(m_boundaries, m_anchor, m_caret);
        return replaceRange(range.begin, range.end, {});
    }

    [[nodiscard]] b8 eraseForward(){
        const EditWordRange range = ResolveEditEraseForwardRange(m_boundaries, m_text.size(), m_anchor, m_caret);
        return replaceRange(range.begin, range.end, {});
    }

    // Native surrounding deletion uses byte distances from the caret and preserves the original selection in undo.
    [[nodiscard]] b8 eraseSurrounding(const usize beforeBytes, const usize afterBytes){
        if(beforeBytes > m_caret || afterBytes > m_text.size() - m_caret)
            return false;
        if(beforeBytes == 0u && afterBytes == 0u)
            return true;
        return replaceRange(m_caret - beforeBytes, m_caret + afterBytes, {});
    }

    // Native selection surrounding deletion retains selected bytes and direction, rejecting merged endpoint seams.
    [[nodiscard]] b8 eraseAroundSelection(const usize beforeBytes, const usize afterBytes){
        const usize start = selectionStart();
        const usize end = selectionEnd();
        if(beforeBytes == 0u && afterBytes == 0u)
            return beforeBytes <= start && afterBytes <= m_text.size() - end;
        if(!CanEraseAroundEditSelection(beforeBytes, afterBytes, start, end, m_text.size(), m_boundaries))
            return false;

        AString<Core::Alloc::GlobalArena> candidate(m_arena);
        BuildErasedAroundSelectionEditString(text(), beforeBytes, afterBytes, start, end, candidate);
        const AStringView after(candidate.data(), candidate.size());
        auto boundaries = buildBoundaries(after);
        if(!boundaries)
            return false;
        return CommitEditModelEraseAroundSelection(
            after,
            *boundaries,
            beforeBytes,
            m_limits.maxHistoryRecords,
            m_limits.maxHistoryBytes,
            m_text,
            m_boundaries,
            m_anchor,
            m_caret,
            m_selectionGeneration,
            m_history,
            m_historyCursor,
            m_historyBytes,
            m_preedit,
            m_preeditAnchor,
            m_preeditCaret,
            m_compositionAnchor,
            m_compositionCaret,
            m_compositionActive,
            m_compositionGeneration,
            m_revision
        );
    }

    [[nodiscard]] b8 undo(){
        if(!canUndo())
            return false;
        const HistoryRecord& record = m_history[m_historyCursor - 1u];
        return restoreHistoryState({ record.before.data(), record.before.size() }, record.beforeAnchor, record.beforeCaret, -1);
    }

    [[nodiscard]] b8 redo(){
        if(!canRedo())
            return false;
        const HistoryRecord& record = m_history[m_historyCursor];
        return restoreHistoryState({ record.after.data(), record.after.size() }, record.afterAnchor, record.afterCaret, 1);
    }

    [[nodiscard]] b8 canUndo()const noexcept{ return m_historyCursor != 0u; }

    [[nodiscard]] b8 canRedo()const noexcept{ return m_historyCursor < m_history.size(); }

    [[nodiscard]] b8 beginComposition()noexcept{
        return ApplyEditModelBeginComposition(
            m_anchor,
            m_caret,
            m_preedit,
            m_preeditAnchor,
            m_preeditCaret,
            m_compositionAnchor,
            m_compositionCaret,
            m_compositionActive,
            m_compositionGeneration
        );
    }

    // Preedit positions are validated UTF8 scalar boundaries, because native IMEs may select part of a grapheme.
    [[nodiscard]] b8 updateComposition(const AStringView value, const usize anchor, const usize caret){
        return ApplyEditModelUpdateComposition(
            m_compositionActive,
            value,
            anchor,
            caret,
            m_text.size(),
            m_compositionAnchor,
            m_compositionCaret,
            m_limits.maxBytes,
            m_textMode,
            m_preedit,
            m_preeditAnchor,
            m_preeditCaret,
            m_compositionGeneration
        );
    }

    [[nodiscard]] b8 commitComposition(const AStringView value){
        if(!m_compositionActive)
            return false;
        return replaceRange(Min(m_compositionAnchor, m_compositionCaret), Max(m_compositionAnchor, m_compositionCaret), value);
    }

    void cancelComposition()noexcept{
        CancelEditModelComposition(
            m_preedit,
            m_preeditAnchor,
            m_preeditCaret,
            m_compositionAnchor,
            m_compositionCaret,
            m_compositionActive,
            m_compositionGeneration
        );
    }

    [[nodiscard]] EditCompositionView composition()const noexcept{
        return { { m_preedit.data(), m_preedit.size() }, m_preeditAnchor, m_preeditCaret, Min(m_compositionAnchor, m_compositionCaret), Max(m_compositionAnchor, m_compositionCaret), m_compositionActive };
    }


private:
    [[nodiscard]] b8 replaceRange(const usize begin, const usize end, const AStringView replacement){
        if(!CanReplaceEditRange(begin, end, replacement.size(), m_text.size(), m_limits.maxBytes, m_boundaries) || !ValidateEditTextForMode(replacement, m_textMode))
            return false;
        AString<Core::Alloc::GlobalArena> candidate(m_arena);
        BuildReplacedEditString(text(), begin, end, replacement, candidate);
        const AStringView after(candidate.data(), candidate.size());
        auto boundaries = buildBoundaries(after);
        if(!boundaries)
            return false;
        // Inserted combining marks and neighboring emoji may merge clusters; keep the caret at a valid following edge.
        CommitEditModelReplaceRange(
            after,
            *boundaries,
            begin,
            replacement.size(),
            m_limits.maxHistoryRecords,
            m_limits.maxHistoryBytes,
            m_text,
            m_boundaries,
            m_anchor,
            m_caret,
            m_selectionGeneration,
            m_history,
            m_historyCursor,
            m_historyBytes,
            m_preedit,
            m_preeditAnchor,
            m_preeditCaret,
            m_compositionAnchor,
            m_compositionCaret,
            m_compositionActive,
            m_compositionGeneration,
            m_revision
        );
        return true;
    }

    [[nodiscard]] Expected<Vector<usize, Core::Alloc::GlobalArena>> buildBoundaries(const AStringView value)const{
        if(m_textMode == EditTextMode::Multiline && !ValidateMultilineText(value))
            return MakeUnexpected(Failure{});
        return GraphemeSegmentation::Build(m_arena, value, m_textMode == EditTextMode::SingleLine);
    }

    [[nodiscard]] b8 restoreHistoryState(const AStringView value, const usize anchor, const usize caret, const isize cursorDelta){
        auto boundaries = buildBoundaries(value);
        if(!boundaries)
            return false;
        CommitEditModelRestoreHistoryState(
            value,
            *boundaries,
            anchor,
            caret,
            cursorDelta,
            m_text,
            m_boundaries,
            m_anchor,
            m_caret,
            m_selectionGeneration,
            m_historyCursor,
            m_preedit,
            m_preeditAnchor,
            m_preeditCaret,
            m_compositionAnchor,
            m_compositionCaret,
            m_compositionActive,
            m_compositionGeneration,
            m_revision
        );
        return true;
    }


private:
    Core::Alloc::GlobalArena& m_arena;
    const u64 m_instanceGeneration;
    const EditLimits m_limits;
    AString<Core::Alloc::GlobalArena> m_text;
    Vector<usize, Core::Alloc::GlobalArena> m_boundaries;
    Vector<HistoryRecord, Core::Alloc::GlobalArena> m_history;
    AString<Core::Alloc::GlobalArena> m_preedit;
    usize m_anchor = 0u;
    usize m_caret = 0u;
    usize m_historyCursor = 0u;
    usize m_historyBytes = 0u;
    usize m_compositionAnchor = 0u;
    usize m_compositionCaret = 0u;
    usize m_preeditAnchor = 0u;
    usize m_preeditCaret = 0u;
    u64 m_revision = 1u;
    u64 m_externalRevision = 1u;
    u64 m_compositionGeneration = 1u;
    u64 m_selectionGeneration = 1u;
    const EditTextMode::Enum m_textMode;
    b8 m_compositionActive = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
