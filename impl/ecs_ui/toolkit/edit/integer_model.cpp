// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "integer_model.h"

#include <global/numeric_text.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


IntegerEditModel::IntegerEditModel(Core::Alloc::GlobalArena& arena)
    : m_draft(arena, { s_NumericEditMaxBytes, s_NumericEditMaxHistoryRecords, s_NumericEditMaxBytes * s_NumericEditHistoryBytesFactor })
    , m_acceptedDraft(arena)
{
    m_acceptedDraft.reserve(s_NumericEditMaxBytes);
    if(!canonicalize(0))
        TerminateInvariant();
}

NumericParseStatus::Enum IntegerEditModel::status(const IntegerBounds& bounds)const noexcept{
    if(!ValidateIntegerBounds(bounds))
        return NumericParseStatus::Invalid;
    return NumericEditDetail::DraftStatus(ParseIntegerDraft(m_draft.text()), bounds);
}

b8 IntegerEditModel::setValue(const i64 value){
    if(!canonicalize(value))
        return false;
    m_value = value;
    NumericEditDetail::AdvanceRevision(m_revision);
    return true;
}

NumericEditResult IntegerEditModel::commit(const IntegerBounds& bounds, const b8 canonical){
    if(!ValidateIntegerBounds(bounds))
        return {};
    NumericEditResult result = NumericEditDetail::CommitDraft(
        ParseIntegerDraft(m_draft.text()), bounds, canonical, m_draft.text(),
        [this](const i64 candidate){ return canonicalize(candidate); },
        [](const i64 candidate, const i64 value)noexcept{ return candidate != value; },
        m_acceptedDraft, m_value
    );
    if(result.valid)
        NumericEditDetail::AdvanceRevision(m_revision);
    return result;
}

Expected<b8, ErrorCode> IntegerEditModel::canonicalize(const i64 value){
    return NumericEditDetail::FormatCanonicalDraft(value, [](const i64 candidate, const Span<char> buffer)noexcept{ return FormatI64(candidate, buffer); }, m_draft, m_acceptedDraft);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

