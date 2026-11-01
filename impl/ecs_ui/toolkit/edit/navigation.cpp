// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "model.h"
#include "grapheme.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_edit_navigation{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static EditNavigationRunClass::Enum Classify(const AStringView text, const usize offset)noexcept{
    const auto decoded = DecodeUtf8CodePoint(text.substr(offset, s_Utf8MaxSequenceBytes));
    if(!decoded)
        return EditNavigationRunClass::Text;
    return ClassifyEditNavigationScalar(decoded->codePoint);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 EditModel::move(const EditMove::Enum movement, const b8 extend)noexcept{
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
        return m_text[byte] == '\n' ? EditNavigationRunClass::LineBreak : Classify(text(), byte);
    };
    const EditNavigationRunClass::Enum run = category(*origin);
    auto first = origin;
    auto last = origin + 1;
    while(first != m_boundaries.begin() && category(*(first - 1)) == run)
        --first;
    while(last != m_boundaries.end() - 1 && category(*last) == run)
        ++last;
    return EditWordRange{ *first, *last };
}

b8 EditModel::isBoundary(const usize position)const noexcept{
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

usize EditModel::wordBoundary(const usize position, const b8 forward)const noexcept{
    using namespace __hidden_ui_edit_navigation;
    if((forward && position == m_text.size()) || (!forward && position == 0u))
        return position;
    auto cursor = LowerBound(m_boundaries.begin(), m_boundaries.end(), position);
    if(forward){
        const EditNavigationRunClass::Enum category = Classify(text(), position);
        while(cursor != m_boundaries.end() - 1 && Classify(text(), *cursor) == category)
            ++cursor;
        while(cursor != m_boundaries.end() - 1 && Classify(text(), *cursor) == EditNavigationRunClass::Whitespace)
            ++cursor;
        return *cursor;
    }
    while(cursor != m_boundaries.begin() && Classify(text(), *(cursor - 1)) == EditNavigationRunClass::Whitespace)
        --cursor;
    if(cursor == m_boundaries.begin())
        return 0u;
    const EditNavigationRunClass::Enum category = Classify(text(), *(cursor - 1));
    while(cursor != m_boundaries.begin() && Classify(text(), *(cursor - 1)) == category)
        --cursor;
    return *cursor;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

