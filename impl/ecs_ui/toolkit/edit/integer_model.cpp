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
    const auto formatted = FormatI64(value, buffer);
    if(!formatted)
        return MakeUnexpected(formatted.error());
    const bool changed = *formatted != m_draft.text();
    if(!m_draft.setText(*formatted))
        return MakeUnexpected(MakeErrorCode(ErrorCondition::invalid_argument));
    m_acceptedDraft.assign(formatted->data(), formatted->size());
    return changed;
}

void IntegerEditModel::advanceRevision()noexcept{
    if(m_revision == Limit<u64>::s_Max)
        TerminateInvariant();
    ++m_revision;
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
    auto parsed = ParseIntegerDraft(m_draft.text());
    if(parsed && (*parsed < bounds.minimum || *parsed > bounds.maximum))
        return NumericParseStatus::OutOfRange;
    return parsed ? NumericParseStatus::Complete : parsed.error();
}

bool IntegerEditModel::dirty()const noexcept{
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
    NumericEditResult result = NumericEditDetail::RestoreDraft([this](){ return canonicalize(m_value); }, true);
    if(result.valid)
        advanceRevision();
    return result;
}

NumericEditResult IntegerEditModel::abandon(){
    NumericEditResult result = NumericEditDetail::RestoreDraft([this](){ return canonicalize(m_value); }, false);
    if(result.valid)
        advanceRevision();
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

