// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "integer_model.h"

#include <global/numeric_text.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


IntegerEditModel::IntegerEditModel(Core::Alloc::GlobalArena& arena)
    : NumericEditModelBase(arena)
{
    initializeDefault(0);
}

NumericParseStatus::Enum IntegerEditModel::status(const IntegerBounds& bounds)const noexcept{
    return evaluateDraftStatus(bounds, ValidateIntegerBounds, ParseIntegerDraft);
}

b8 IntegerEditModel::setValue(const i64 value){
    return assignAuthoritativeValue(value);
}

NumericEditResult IntegerEditModel::commit(const IntegerBounds& bounds, const b8 canonical){
    return commitDraftWith(
        bounds,
        canonical,
        ValidateIntegerBounds,
        ParseIntegerDraft,
        [](const i64 candidate, const i64 value)noexcept{ return candidate != value; }
    );
}

Expected<b8, ErrorCode> IntegerEditModel::canonicalize(const i64 value){
    return canonicalizeWith(value, [](const i64 candidate, const Span<char> buffer)noexcept{ return FormatI64(candidate, buffer); });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

