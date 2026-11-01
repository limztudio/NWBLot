// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "float_model.h"

#include <global/bit.h>
#include <global/numeric_text.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


FloatEditModel::FloatEditModel(Core::Alloc::GlobalArena& arena)
    : m_draft(arena, { s_NumericEditMaxBytes, s_NumericEditMaxHistoryRecords, s_NumericEditMaxBytes * s_NumericEditHistoryBytesFactor })
    , m_acceptedDraft(arena)
{
    m_acceptedDraft.reserve(s_NumericEditMaxBytes);
    if(!canonicalize(0.0))
        TerminateInvariant();
}

NumericParseStatus::Enum FloatEditModel::status(const FloatBounds& bounds)const noexcept{
    if(!ValidateFloatBounds(bounds))
        return NumericParseStatus::Invalid;
    return NumericEditDetail::DraftStatus(ParseFloatDraft(m_draft.text()), bounds);
}

b8 FloatEditModel::setValue(const f64 value){
    if(!IsFinite(value) || !canonicalize(value))
        return false;
    m_value = value;
    NumericEditDetail::AdvanceRevision(m_revision);
    return true;
}

NumericEditResult FloatEditModel::commit(const FloatBounds& bounds, const b8 canonical){
    if(!ValidateFloatBounds(bounds))
        return {};
    NumericEditResult result = NumericEditDetail::CommitDraft(
        ParseFloatDraft(m_draft.text()), bounds, canonical, m_draft.text(),
        [this](const f64 candidate){ return canonicalize(candidate); },
        [](const f64 candidate, const f64 value)noexcept{ return BitCast<u64>(candidate) != BitCast<u64>(value); },
        m_acceptedDraft, m_value
    );
    if(result.valid)
        NumericEditDetail::AdvanceRevision(m_revision);
    return result;
}

Expected<b8, ErrorCode> FloatEditModel::canonicalize(const f64 value){
    return NumericEditDetail::FormatCanonicalDraft(value, [](const f64 candidate, const Span<char> buffer)noexcept{ return FormatF64(candidate, buffer); }, m_draft, m_acceptedDraft);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

