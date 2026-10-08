// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "integer_model.h"

#include <global/text_numeric_format.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NumericEditResult IntegerEditModel::commit(const IntegerBounds& bounds, const bool canonical){
    if(!ValidateIntegerBounds(bounds))
        return {};
    auto parsed = ParseIntegerDraft(m_draft.text());
    const bool outside = parsed && (*parsed < bounds.minimum || *parsed > bounds.maximum);
    const bool clamped = outside && bounds.policy == NumericBoundsPolicy::Clamp;
    const bool rejected = !parsed || (outside && !clamped);
    NumericEditResult result;
    if(rejected){
        if(canonical){
            const auto restored = canonicalize(m_value);
            if(!restored)
                return result;
            result.restored = *restored;
        }
        result.valid = true;
        result.rejected = true;
        advanceRevision();
        return result;
    }
    i64 candidate = *parsed;
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
    result.valueChanged = candidate != m_value;
    result.clamped = clamped;
    m_value = candidate;
    advanceRevision();
    return result;
}

Expected<bool> IntegerEditModel::canonicalize(const i64 value){
    char buffer[s_NumericEditMaxBytes];
    const AStringView formatted = FormatI64(value, buffer);
    const bool changed = formatted != m_draft.text();
    if(formatted.empty() || !m_draft.setText(formatted))
        return MakeUnexpected(Failure{});
    m_acceptedDraft.assign(formatted.data(), formatted.size());
    return changed;
}

void IntegerEditModel::advanceRevision()noexcept{
    if(m_revision == Limit<u64>::s_Max)
        TerminateInvariant();
    ++m_revision;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


IntegerEditModel::IntegerEditModel(Core::Alloc::GlobalArena& arena)
    : m_draft(arena, { s_NumericEditMaxBytes, 64u, s_NumericEditMaxBytes * 128u })
    , m_acceptedDraft(arena)
{
    m_acceptedDraft.reserve(s_NumericEditMaxBytes);
    if(!canonicalize(0))
        TerminateInvariant();
}

NumericParseStatus::Enum IntegerEditModel::status(const IntegerBounds& bounds)const{
    if(!ValidateIntegerBounds(bounds))
        return NumericParseStatus::Invalid;
    auto parsed = ParseIntegerDraft(m_draft.text());
    if(parsed && (*parsed < bounds.minimum || *parsed > bounds.maximum))
        return NumericParseStatus::OutOfRange;
    return parsed ? NumericParseStatus::Complete : parsed.error();
}

bool IntegerEditModel::dirty()const{
    return m_draft.text() != AStringView(m_acceptedDraft.data(), m_acceptedDraft.size());
}

bool IntegerEditModel::setValue(const i64 value){
    if(!canonicalize(value))
        return false;
    m_value = value;
    advanceRevision();
    return true;
}

bool IntegerEditModel::setDraft(const AStringView text){
    if(!m_draft.setText(text))
        return false;
    advanceRevision();
    return true;
}

NumericEditResult IntegerEditModel::submit(const IntegerBounds& bounds){ return commit(bounds, false); }

NumericEditResult IntegerEditModel::blur(const IntegerBounds& bounds){ return commit(bounds, true); }

NumericEditResult IntegerEditModel::cancel(){
    NumericEditResult result;
    const auto restored = canonicalize(m_value);
    if(!restored)
        return result;
    result.restored = *restored;
    result.valid = true;
    result.cancelled = true;
    advanceRevision();
    return result;
}

NumericEditResult IntegerEditModel::abandon(){
    NumericEditResult result;
    const auto restored = canonicalize(m_value);
    if(!restored)
        return result;
    result.restored = *restored;
    result.valid = true;
    advanceRevision();
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

