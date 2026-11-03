// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/toolkit/global.h>

#include <impl/ecs_ui/toolkit/edit/types.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// UTF8 byte selections always lie on Unicode 17 extended grapheme boundaries; preedit stays separate from committed text.
class EditModel final : NoCopy{
private:
    struct HistoryRecord{
        AString<Core::Alloc::GlobalArena> before;
        AString<Core::Alloc::GlobalArena> after;
        usize beforeAnchor = 0u;
        usize beforeCaret = 0u;
        usize afterAnchor = 0u;
        usize afterCaret = 0u;


    public:
        explicit HistoryRecord(Core::Alloc::GlobalArena& arena)noexcept
            : before(arena)
            , after(arena)
        {}
    };


public:
    explicit EditModel(Core::Alloc::GlobalArena& arena, const EditLimits& limits = {}, EditTextMode::Enum mode = EditTextMode::SingleLine);


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
    [[nodiscard]] b8 setText(AStringView value);
    [[nodiscard]] b8 setSelection(usize anchor, usize caret)noexcept;
    [[nodiscard]] b8 selectAll()noexcept{ return setSelection(0u, m_text.size()); }
    // Home/End address the current hard line in Multiline mode; document movement always addresses the complete text.
    // Word movement groups whitespace, ASCII punctuation, and all remaining graphemes without a linguistic word claim.
    [[nodiscard]] b8 move(const EditMove::Enum movement, const b8 extend = false)noexcept{
        const auto target = TryResolveEditMoveTarget(text(), m_boundaries, m_textMode, m_anchor, m_caret, movement, extend);
        if(!target)
            return false;
        cancelComposition();
        if(!extend)
            m_anchor = *target;
        m_caret = *target;
        advanceSelectionGeneration();
        return true;
    }
    // Returns the complete run containing a grapheme boundary; document end selects the preceding run.
    [[nodiscard]] Expected<EditWordRange> wordRangeAt(const usize position)const noexcept{
        return ResolveEditWordRangeAt(text(), m_boundaries, position);
    }
    [[nodiscard]] b8 replaceSelection(const AStringView value){ return replaceRange(selectionStart(), selectionEnd(), value); }
    [[nodiscard]] b8 backspace(){
        return replaceRange(hasSelection() ? selectionStart() : previousBoundary(m_caret), hasSelection() ? selectionEnd() : m_caret, {});
    }
    [[nodiscard]] b8 eraseForward(){
        return replaceRange(hasSelection() ? selectionStart() : m_caret, hasSelection() ? selectionEnd() : nextBoundary(m_caret), {});
    }
    // Native surrounding deletion uses byte distances from the caret and preserves the original selection in undo.
    [[nodiscard]] b8 eraseSurrounding(usize beforeBytes, usize afterBytes);
    // Native selection surrounding deletion retains selected bytes and direction, rejecting merged endpoint seams.
    [[nodiscard]] b8 eraseAroundSelection(usize beforeBytes, usize afterBytes);
    [[nodiscard]] b8 undo();
    [[nodiscard]] b8 redo();
    [[nodiscard]] b8 canUndo()const noexcept{ return m_historyCursor != 0u; }

    [[nodiscard]] b8 canRedo()const noexcept{ return m_historyCursor < m_history.size(); }
    [[nodiscard]] b8 beginComposition()noexcept;
    // Preedit positions are validated UTF8 scalar boundaries, because native IMEs may select part of a grapheme.
    [[nodiscard]] b8 updateComposition(AStringView value, usize anchor, usize caret);
    [[nodiscard]] b8 commitComposition(AStringView value);
    void cancelComposition()noexcept;
    [[nodiscard]] EditCompositionView composition()const noexcept{
        return { { m_preedit.data(), m_preedit.size() }, m_preeditAnchor, m_preeditCaret, Min(m_compositionAnchor, m_compositionCaret), Max(m_compositionAnchor, m_compositionCaret), m_compositionActive };
    }


private:
    [[nodiscard]] b8 isBoundary(const usize position)const noexcept{ return IsEditBoundary(m_boundaries, position); }
    [[nodiscard]] usize previousBoundary(const usize position)const noexcept{ return PreviousEditBoundary(m_boundaries, position); }
    [[nodiscard]] usize nextBoundary(const usize position)const noexcept{ return NextEditBoundary(m_boundaries, m_text.size(), position); }
    [[nodiscard]] usize wordBoundary(const usize position, const b8 forward)const noexcept{ return ResolveEditWordBoundary(text(), m_boundaries, position, forward); }
    [[nodiscard]] b8 replaceRange(usize begin, usize end, AStringView replacement);
    [[nodiscard]] b8 validateText(AStringView value)const noexcept;
    [[nodiscard]] Expected<Vector<usize, Core::Alloc::GlobalArena>> buildBoundaries(AStringView value)const;
    [[nodiscard]] b8 restoreHistoryState(AStringView value, usize anchor, usize caret, isize cursorDelta);
    void recordHistory(AStringView before, usize beforeAnchor, usize beforeCaret, AStringView after, usize afterAnchor, usize afterCaret);
    void clearHistory()noexcept;
    void advanceRevision()noexcept{ AdvanceRevision(m_revision); }
    void advanceSelectionGeneration()noexcept{ AdvanceRevision(m_selectionGeneration); }
    void advanceCompositionGeneration()noexcept{ AdvanceRevision(m_compositionGeneration); }


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

