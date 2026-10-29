// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

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
    [[nodiscard]] b8 setText(AStringView value);
    [[nodiscard]] b8 setSelection(usize anchor, usize caret)noexcept;
    [[nodiscard]] b8 selectAll()noexcept;
    // Home/End address the current hard line in Multiline mode; document movement always addresses the complete text.
    // Word movement groups whitespace, ASCII punctuation, and all remaining graphemes without a linguistic word claim.
    [[nodiscard]] b8 move(EditMove::Enum movement, b8 extend = false)noexcept;
    // Returns the complete run containing a grapheme boundary; document end selects the preceding run.
    [[nodiscard]] Expected<EditWordRange> wordRangeAt(usize position)const noexcept;
    [[nodiscard]] b8 replaceSelection(AStringView value);
    [[nodiscard]] b8 backspace();
    [[nodiscard]] b8 eraseForward();
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
    [[nodiscard]] EditCompositionView composition()const noexcept;


private:
    [[nodiscard]] b8 isBoundary(usize position)const noexcept;
    [[nodiscard]] usize previousBoundary(usize position)const noexcept;
    [[nodiscard]] usize nextBoundary(usize position)const noexcept;
    [[nodiscard]] usize wordBoundary(usize position, b8 forward)const noexcept;
    [[nodiscard]] b8 replaceRange(usize begin, usize end, AStringView replacement);
    [[nodiscard]] b8 validateText(AStringView value)const noexcept;
    [[nodiscard]] Expected<Vector<usize, Core::Alloc::GlobalArena>> buildBoundaries(AStringView value)const;
    void recordHistory(AStringView before, usize beforeAnchor, usize beforeCaret, AStringView after, usize afterAnchor, usize afterCaret);
    void clearHistory()noexcept;
    void advanceRevision()noexcept;
    void advanceSelectionGeneration()noexcept;
    void advanceCompositionGeneration()noexcept;


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

