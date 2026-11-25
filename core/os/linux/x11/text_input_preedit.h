// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/alloc/general.h>
#include <core/os/text_input.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class X11PreeditBuffer final : private NoCopy{
public:
    static constexpr usize s_MaxBytes = 64u * 1024u;


public:
    explicit X11PreeditBuffer(Alloc::GlobalArena& arena)noexcept;


public:
    [[nodiscard]] AStringView text()const noexcept{ return m_text; }

    [[nodiscard]] usize caretByte()const noexcept{ return m_caretByte; }
    [[nodiscard]] Span<const TextInputPreeditClause> clauses()const noexcept{ return m_clauses; }
    void clear()noexcept;
    [[nodiscard]] b8 replace(usize firstCharacter, usize characterCount, AStringView insertion, usize caretCharacter, Span<const TextInputPreeditClauseStyle::Enum> insertionStyles = {});
    [[nodiscard]] b8 updateFeedback(usize firstCharacter, usize characterCount, Span<const TextInputPreeditClauseStyle::Enum> styles, usize caretCharacter)noexcept;
    [[nodiscard]] b8 moveCaret(usize caretCharacter)noexcept;
    [[nodiscard]] usize moveCaretToEnd()noexcept;


private:
    AString<Alloc::GlobalArena> m_text;
    AString<Alloc::GlobalArena> m_candidate;
    Vector<TextInputPreeditClauseStyle::Enum, Alloc::GlobalArena> m_scalarStyles;
    Vector<TextInputPreeditClauseStyle::Enum, Alloc::GlobalArena> m_scalarStylesCandidate;
    Vector<TextInputPreeditClause, Alloc::GlobalArena> m_clauses;
    b8 m_hasFeedback = false;
    usize m_caretByte = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
