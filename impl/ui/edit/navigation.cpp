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
    enum Enum : u8{ Whitespace, Punctuation, Text };
};

static bool IsWhitespace(const u32 scalar){
    return (scalar >= 0x9u && scalar <= 0xDu) || scalar == 0x20u || scalar == 0x85u || scalar == 0xA0u
        || scalar == 0x1680u || (scalar >= 0x2000u && scalar <= 0x200Au) || scalar == 0x2028u
        || scalar == 0x2029u || scalar == 0x202Fu || scalar == 0x205Fu || scalar == 0x3000u;
}

static RunClass::Enum Classify(const AStringView text, const usize offset){
    u32 scalar = 0u;
    const i32 decoded = DecodeUtf8CodePoint(text.data() + offset, static_cast<i32>(Min<usize>(text.size() - offset, 4u)), scalar);
    if(decoded <= 0)
        return RunClass::Text;
    if(IsWhitespace(scalar))
        return RunClass::Whitespace;
    if(
        (scalar >= 0x21u && scalar <= 0x2Fu) || (scalar >= 0x3Au && scalar <= 0x40u)
        || (scalar >= 0x5Bu && scalar <= 0x60u) || (scalar >= 0x7Bu && scalar <= 0x7Eu)
    )
        return RunClass::Punctuation;
    return RunClass::Text;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool EditModel::move(const EditMove::Enum movement, const bool extend){
    if(movement > EditMove::WordRight)
        return false;
    usize target = m_caret;
    if(!extend && hasSelection() && (movement == EditMove::Left || movement == EditMove::Right))
        target = movement == EditMove::Left ? selectionStart() : selectionEnd();
    else{
        switch(movement){
        case EditMove::Left: target = previousBoundary(target); break;
        case EditMove::Right: target = nextBoundary(target); break;
        case EditMove::Home: target = 0u; break;
        case EditMove::End: target = m_text.size(); break;
        case EditMove::WordLeft: target = wordBoundary(target, false); break;
        case EditMove::WordRight: target = wordBoundary(target, true); break;
        }
    }
    cancelComposition();
    if(!extend)
        m_anchor = target;
    m_caret = target;
    return true;
}

bool EditModel::isBoundary(const usize position)const{
    const auto found = LowerBound(m_boundaries.begin(), m_boundaries.end(), position);
    return found != m_boundaries.end() && *found == position;
}

usize EditModel::previousBoundary(const usize position)const{
    const auto found = LowerBound(m_boundaries.begin(), m_boundaries.end(), position);
    return found == m_boundaries.begin() ? 0u : *(found - 1);
}

usize EditModel::nextBoundary(const usize position)const{
    auto found = LowerBound(m_boundaries.begin(), m_boundaries.end(), position);
    if(found != m_boundaries.end() && *found == position)
        ++found;
    return found == m_boundaries.end() ? m_text.size() : *found;
}

usize EditModel::wordBoundary(usize position, const bool forward)const{
    using namespace __hidden_ui_edit_navigation;
    if(forward){
        if(position == m_text.size())
            return position;
        const RunClass::Enum category = Classify(text(), position);
        while(position < m_text.size() && Classify(text(), position) == category)
            position = nextBoundary(position);
        while(position < m_text.size() && Classify(text(), position) == RunClass::Whitespace)
            position = nextBoundary(position);
        return position;
    }
    while(position != 0u && Classify(text(), previousBoundary(position)) == RunClass::Whitespace)
        position = previousBoundary(position);
    if(position == 0u)
        return position;
    const RunClass::Enum category = Classify(text(), previousBoundary(position));
    while(position != 0u && Classify(text(), previousBoundary(position)) == category)
        position = previousBoundary(position);
    return position;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

