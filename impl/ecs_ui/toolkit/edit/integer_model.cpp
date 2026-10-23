// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "integer_model.h"
#include "numeric_model_operations.h"

#include <global/numeric_text.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NumericEditResult IntegerEditModel::commit(const IntegerBounds& bounds, const bool canonical){
    if(!ValidateIntegerBounds(bounds))
        return {};
    NumericEditResult result = NumericEditDetail::CommitDraft(
        m_value, m_acceptedDraft, ParseIntegerDraft(m_draft.text()), bounds, canonical, m_draft.text(),
        [this](const i64 candidate){ return canonicalize(candidate); },
        [](const i64 candidate, const i64 value)noexcept{ return candidate != value; }
    );
    if(result.valid)
        advanceRevision();
    return result;
}

Expected<bool, ErrorCode> IntegerEditModel::canonicalize(const i64 value){
    char buffer[s_NumericEditMaxBytes];
    return NumericEditDetail::CanonicalizeDraft(FormatI64(value, buffer), m_draft, m_acceptedDraft);
}

void IntegerEditModel::advanceRevision()noexcept{
    NumericEditDetail::AdvanceRevision(m_revision);
}


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

bool IntegerEditModel::dirty()const noexcept{
    return NumericEditDetail::DraftDirty(m_draft, m_acceptedDraft);
}

bool IntegerEditModel::setValue(const i64 value){
    if(!canonicalize(value))
        return false;
    m_value = value;
    advanceRevision();
    return true;
}

bool IntegerEditModel::setDraft(const AStringView text){
    return NumericEditDetail::SetDraftText(text, m_draft, m_revision);
}

NumericEditResult IntegerEditModel::submit(const IntegerBounds& bounds){ return commit(bounds, false); }

NumericEditResult IntegerEditModel::blur(const IntegerBounds& bounds){ return commit(bounds, true); }

NumericEditResult IntegerEditModel::cancel(){
    return NumericEditDetail::RestoreModelDraft([this](){ return canonicalize(m_value); }, true, m_revision);
}

NumericEditResult IntegerEditModel::abandon(){
    return NumericEditDetail::RestoreModelDraft([this](){ return canonicalize(m_value); }, false, m_revision);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

