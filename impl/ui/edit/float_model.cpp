// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "float_model.h"

#include <global/bit.h>
#include <global/text_numeric_format.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NumericEditResult FloatEditModel::commit(const FloatBounds& bounds, const bool canonical){
    if(!ValidateFloatBounds(bounds))
        return {};
    f64 candidate = 0.0;
    const NumericParseStatus::Enum parsed = ParseFloatDraft(m_draft.text(), candidate);
    const bool outside = parsed == NumericParseStatus::Complete && (candidate < bounds.minimum || candidate > bounds.maximum);
    const bool clamped = outside && bounds.policy == NumericBoundsPolicy::Clamp;
    const bool rejected = parsed != NumericParseStatus::Complete || (outside && !clamped);
    NumericEditResult result;
    if(rejected){
        if(canonical && !canonicalize(m_value, &result.restored))
            return result;
        result.valid = true;
        result.rejected = true;
        advanceRevision();
        return result;
    }
    if(clamped)
        candidate = candidate < bounds.minimum ? bounds.minimum : bounds.maximum;
    if(canonical || clamped){
        if(!canonicalize(candidate))
            return result;
    }
    else
        m_acceptedDraft.assign(m_draft.text().data(), m_draft.text().size());
    result.valid = true;
    result.committed = true;
    result.valueChanged = BitCast<u64>(candidate) != BitCast<u64>(m_value);
    result.clamped = clamped;
    m_value = candidate;
    advanceRevision();
    return result;
}

bool FloatEditModel::canonicalize(const f64 value, bool* textChanged){
    char buffer[s_NumericEditMaxBytes];
    const AStringView formatted = FormatF64(value, buffer);
    const bool changed = formatted != m_draft.text();
    if(formatted.empty() || !m_draft.setText(formatted))
        return false;
    m_acceptedDraft.assign(formatted.data(), formatted.size());
    if(textChanged)
        *textChanged = changed;
    return true;
}

void FloatEditModel::advanceRevision(){
    if(m_revision == Limit<u64>::s_Max)
        TerminateInvariant();
    ++m_revision;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


FloatEditModel::FloatEditModel(Core::Alloc::GlobalArena& arena)
    : m_draft(arena, { s_NumericEditMaxBytes, 64u, s_NumericEditMaxBytes * 128u })
    , m_acceptedDraft(arena)
{
    m_acceptedDraft.reserve(s_NumericEditMaxBytes);
    if(!canonicalize(0.0))
        TerminateInvariant();
}

NumericParseStatus::Enum FloatEditModel::status(const FloatBounds& bounds)const{
    if(!ValidateFloatBounds(bounds))
        return NumericParseStatus::Invalid;
    f64 candidate = 0.0;
    const NumericParseStatus::Enum parsed = ParseFloatDraft(m_draft.text(), candidate);
    if(parsed == NumericParseStatus::Complete && (candidate < bounds.minimum || candidate > bounds.maximum))
        return NumericParseStatus::OutOfRange;
    return parsed;
}

bool FloatEditModel::dirty()const{
    return m_draft.text() != AStringView(m_acceptedDraft.data(), m_acceptedDraft.size());
}

bool FloatEditModel::setValue(const f64 value){
    if(!IsFinite(value) || !canonicalize(value))
        return false;
    m_value = value;
    advanceRevision();
    return true;
}

bool FloatEditModel::setDraft(const AStringView text){
    if(!m_draft.setText(text))
        return false;
    advanceRevision();
    return true;
}

NumericEditResult FloatEditModel::submit(const FloatBounds& bounds){ return commit(bounds, false); }

NumericEditResult FloatEditModel::blur(const FloatBounds& bounds){ return commit(bounds, true); }

NumericEditResult FloatEditModel::cancel(){
    NumericEditResult result;
    if(!canonicalize(m_value, &result.restored))
        return result;
    result.valid = true;
    result.cancelled = true;
    advanceRevision();
    return result;
}

NumericEditResult FloatEditModel::abandon(){
    NumericEditResult result;
    if(!canonicalize(m_value, &result.restored))
        return result;
    result.valid = true;
    advanceRevision();
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

