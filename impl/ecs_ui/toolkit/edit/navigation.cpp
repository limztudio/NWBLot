// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "model.h"
#include "grapheme.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_edit_navigation{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RunClass{
    enum Enum : u8{ Whitespace, Punctuation, Text, LineBreak };
};

static bool IsWhitespace(const u32 scalar)noexcept{
    return (scalar >= 0x9u && scalar <= 0xDu) || scalar == 0x20u || scalar == 0x85u || scalar == 0xA0u
        || scalar == 0x1680u || (scalar >= 0x2000u && scalar <= 0x200Au) || scalar == 0x2028u
        || scalar == 0x2029u || scalar == 0x202Fu || scalar == 0x205Fu || scalar == 0x3000u;
}

static RunClass::Enum Classify(const AStringView text, const usize offset)noexcept{
    const auto decoded = DecodeUtf8CodePoint(text.substr(offset, 4u));
    if(!decoded)
        return RunClass::Text;
    const u32 scalar = decoded->codePoint;
    if(IsWhitespace(scalar))
        return RunClass::Whitespace;
    if((scalar >= 0x21u && scalar <= 0x2Fu) || (scalar >= 0x3Au && scalar <= 0x40u) || (scalar >= 0x5Bu && scalar <= 0x60u) || (scalar >= 0x7Bu && scalar <= 0x7Eu))
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

