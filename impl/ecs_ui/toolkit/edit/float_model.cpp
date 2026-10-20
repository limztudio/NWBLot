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


NumericEditResult FloatEditModel::commit(const FloatBounds& bounds, const bool canonical){
    if(!ValidateFloatBounds(bounds))
        return {};
    NumericEditResult result = NumericEditDetail::CommitDraft(
        m_value, ParseFloatDraft(m_draft.text()), bounds, canonical, m_draft.text(), m_acceptedDraft,
        [this](const f64 candidate){ return canonicalize(candidate); },
        [](const f64 candidate, const f64 value)noexcept{ return BitCast<u64>(candidate) != BitCast<u64>(value); }
    );
    if(result.valid)
        advanceRevision();
    return result;
}

Expected<bool, ErrorCode> FloatEditModel::canonicalize(const f64 value){
    char buffer[s_NumericEditMaxBytes];
    const auto formatted = FormatF64(value, buffer);
    if(!formatted)
        return MakeUnexpected(formatted.error());
    const bool changed = *formatted != m_draft.text();
    if(!m_draft.setText(*formatted))
        return MakeUnexpected(MakeErrorCode(ErrorCondition::invalid_argument));
    m_acceptedDraft.assign(formatted->data(), formatted->size());
    return changed;
}

void FloatEditModel::advanceRevision()noexcept{
    if(m_revision == Limit<u64>::s_Max)
        TerminateInvariant();
    ++m_revision;
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
    auto parsed = ParseFloatDraft(m_draft.text());
    if(parsed && (*parsed < bounds.minimum || *parsed > bounds.maximum))
        return NumericParseStatus::OutOfRange;
    return parsed ? NumericParseStatus::Complete : parsed.error();
}

bool FloatEditModel::dirty()const noexcept{
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
    NumericEditResult result = NumericEditDetail::RestoreDraft([this](){ return canonicalize(m_value); }, true);
    if(result.valid)
        advanceRevision();
    return result;
}

NumericEditResult FloatEditModel::abandon(){
    NumericEditResult result = NumericEditDetail::RestoreDraft([this](){ return canonicalize(m_value); }, false);
    if(result.valid)
        advanceRevision();
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

