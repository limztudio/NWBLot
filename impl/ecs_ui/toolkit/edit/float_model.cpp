// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "float_model.h"
#include "numeric_model_operations.h"

#include <global/bit.h>
#include <global/numeric_text.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NumericEditResult FloatEditModel::commit(const FloatBounds& bounds, const b8 canonical){
    if(!ValidateFloatBounds(bounds))
        return {};
    NumericEditResult result = NumericEditDetail::CommitDraft(
        m_value, m_acceptedDraft, ParseFloatDraft(m_draft.text()), bounds, canonical, m_draft.text(),
        [this](const f64 candidate){ return canonicalize(candidate); },
        [](const f64 candidate, const f64 value)noexcept{ return BitCast<u64>(candidate) != BitCast<u64>(value); }
    );
    if(result.valid)
        advanceRevision();
    return result;
}

Expected<b8, ErrorCode> FloatEditModel::canonicalize(const f64 value){
    char buffer[s_NumericEditMaxBytes];
    return NumericEditDetail::CanonicalizeDraft(FormatF64(value, buffer), m_draft, m_acceptedDraft);
}

void FloatEditModel::advanceRevision()noexcept{
    NumericEditDetail::AdvanceRevision(m_revision);
}


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

b8 FloatEditModel::dirty()const noexcept{
    return NumericEditDetail::DraftDirty(m_draft, m_acceptedDraft);
}

b8 FloatEditModel::setValue(const f64 value){
    if(!IsFinite(value) || !canonicalize(value))
        return false;
    m_value = value;
    advanceRevision();
    return true;
}

b8 FloatEditModel::setDraft(const AStringView text){
    return NumericEditDetail::SetDraftText(text, m_draft, m_revision);
}

NumericEditResult FloatEditModel::submit(const FloatBounds& bounds){ return commit(bounds, false); }

NumericEditResult FloatEditModel::blur(const FloatBounds& bounds){ return commit(bounds, true); }

NumericEditResult FloatEditModel::cancel(){
    return NumericEditDetail::RestoreModelDraft([this](){ return canonicalize(m_value); }, true, m_revision);
}

NumericEditResult FloatEditModel::abandon(){
    return NumericEditDetail::RestoreModelDraft([this](){ return canonicalize(m_value); }, false, m_revision);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

