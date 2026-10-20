// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "model.h"
#include "grapheme.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_edit_navigation{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_UnicodeNoBreakSpace = 0xA0u;
inline constexpr u32 s_UnicodeOghamSpaceMark = 0x1680u;
inline constexpr u32 s_UnicodeEnQuad = 0x2000u;
inline constexpr u32 s_UnicodeHairSpace = 0x200Au;
inline constexpr u32 s_UnicodeNarrowNoBreakSpace = 0x202Fu;
inline constexpr u32 s_UnicodeMediumMathematicalSpace = 0x205Fu;
inline constexpr u32 s_UnicodeIdeographicSpace = 0x3000u;
inline constexpr u32 s_AsciiExclamation = 0x21u;
inline constexpr u32 s_AsciiSlash = 0x2Fu;
inline constexpr u32 s_AsciiColon = 0x3Au;
inline constexpr u32 s_AsciiAt = 0x40u;
inline constexpr u32 s_AsciiLeftBracket = 0x5Bu;
inline constexpr u32 s_AsciiGrave = 0x60u;
inline constexpr u32 s_AsciiLeftBrace = 0x7Bu;
inline constexpr u32 s_AsciiTilde = 0x7Eu;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RunClass{
    enum Enum : u8{ Whitespace, Punctuation, Text, LineBreak };
};

static bool IsWhitespace(const u32 scalar)noexcept{
    return
        (scalar >= s_UnicodeTab && scalar <= s_UnicodeCarriageReturn) || scalar == s_UnicodeSpace || scalar == s_UnicodeNextLine || scalar == s_UnicodeNoBreakSpace
        || scalar == s_UnicodeOghamSpaceMark || (scalar >= s_UnicodeEnQuad && scalar <= s_UnicodeHairSpace) || scalar == s_UnicodeLineSeparator
        || scalar == s_UnicodeParagraphSeparator || scalar == s_UnicodeNarrowNoBreakSpace || scalar == s_UnicodeMediumMathematicalSpace || scalar == s_UnicodeIdeographicSpace
    ;
}

static RunClass::Enum Classify(const AStringView text, const usize offset)noexcept{
    const auto decoded = DecodeUtf8CodePoint(text.substr(offset, s_Utf8MaxSequenceBytes));
    if(!decoded)
        return RunClass::Text;
    const u32 scalar = decoded->codePoint;
    if(IsWhitespace(scalar))
        return RunClass::Whitespace;
    if((scalar >= s_AsciiExclamation && scalar <= s_AsciiSlash) || (scalar >= s_AsciiColon && scalar <= s_AsciiAt) || (scalar >= s_AsciiLeftBracket && scalar <= s_AsciiGrave) || (scalar >= s_AsciiLeftBrace && scalar <= s_AsciiTilde))
        return RunClass::Punctuation;
    return RunClass::Text;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool EditModel::move(const EditMove::Enum movement, const bool extend)noexcept{
    if(movement > EditMove::DocumentEnd)
        return false;
    usize target = m_caret;
    if(!extend && hasSelection() && (movement == EditMove::Left || movement == EditMove::Right))
        target = movement == EditMove::Left ? selectionStart() : selectionEnd();
    else{
        switch(movement){
        case EditMove::Left: target = previousBoundary(target); break;
        case EditMove::Right: target = nextBoundary(target); break;
        case EditMove::Home:
            if(m_textMode == EditTextMode::SingleLine)
                target = 0u;
            else{
                while(target != 0u && m_text[target - 1u] != '\n')
                    --target;
            }
            break;
        case EditMove::End:
            if(m_textMode == EditTextMode::SingleLine)
                target = m_text.size();
            else{
                while(target < m_text.size() && m_text[target] != '\n')
                    ++target;
            }
            break;
        case EditMove::WordLeft: target = wordBoundary(target, false); break;
        case EditMove::WordRight: target = wordBoundary(target, true); break;
        case EditMove::DocumentHome: target = 0u; break;
        case EditMove::DocumentEnd: target = m_text.size(); break;
        }
    }
    cancelComposition();
    if(!extend)
        m_anchor = target;
    m_caret = target;
    advanceSelectionGeneration();
    return true;
}

Expected<EditWordRange> EditModel::wordRangeAt(const usize position)const noexcept{
    using namespace __hidden_ui_edit_navigation;
    const auto requested = LowerBound(m_boundaries.begin(), m_boundaries.end(), position);
    if(requested == m_boundaries.end() || *requested != position)
        return MakeUnexpected(Failure{});
    if(m_text.empty())
        return EditWordRange{};
    const auto origin = position == m_text.size() ? requested - 1 : requested;
    const auto category = [this](const usize byte)noexcept{
        return m_text[byte] == '\n' ? RunClass::LineBreak : Classify(text(), byte);
    };
    const RunClass::Enum run = category(*origin);
    auto first = origin;
    auto last = origin + 1;
    while(first != m_boundaries.begin() && category(*(first - 1)) == run)
        --first;
    while(last != m_boundaries.end() - 1 && category(*last) == run)
        ++last;
    return EditWordRange{ *first, *last };
}

bool EditModel::isBoundary(const usize position)const noexcept{
    const auto found = LowerBound(m_boundaries.begin(), m_boundaries.end(), position);
    return found != m_boundaries.end() && *found == position;
}

usize EditModel::previousBoundary(const usize position)const noexcept{
    const auto found = LowerBound(m_boundaries.begin(), m_boundaries.end(), position);
    return found == m_boundaries.begin() ? 0u : *(found - 1);
}

usize EditModel::nextBoundary(const usize position)const noexcept{
    auto found = LowerBound(m_boundaries.begin(), m_boundaries.end(), position);
    if(found != m_boundaries.end() && *found == position)
        ++found;
    return found == m_boundaries.end() ? m_text.size() : *found;
}

usize EditModel::wordBoundary(const usize position, const bool forward)const noexcept{
    using namespace __hidden_ui_edit_navigation;
    if((forward && position == m_text.size()) || (!forward && position == 0u))
        return position;
    auto cursor = LowerBound(m_boundaries.begin(), m_boundaries.end(), position);
    if(forward){
        const RunClass::Enum category = Classify(text(), position);
        while(cursor != m_boundaries.end() - 1 && Classify(text(), *cursor) == category)
            ++cursor;
        while(cursor != m_boundaries.end() - 1 && Classify(text(), *cursor) == RunClass::Whitespace)
            ++cursor;
        return *cursor;
    }
    while(cursor != m_boundaries.begin() && Classify(text(), *(cursor - 1)) == RunClass::Whitespace)
        --cursor;
    if(cursor == m_boundaries.begin())
        return 0u;
    const RunClass::Enum category = Classify(text(), *(cursor - 1));
    while(cursor != m_boundaries.begin() && Classify(text(), *(cursor - 1)) == category)
        --cursor;
    return *cursor;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

