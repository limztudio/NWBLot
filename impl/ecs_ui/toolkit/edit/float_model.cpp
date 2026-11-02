// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "float_model.h"

#include <global/bit.h>
#include <global/numeric_text.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


FloatEditModel::FloatEditModel(Core::Alloc::GlobalArena& arena)
    : NumericEditModelBase(arena)
{
    initializeDefault(0.0);
}

NumericParseStatus::Enum FloatEditModel::status(const FloatBounds& bounds)const noexcept{
    return evaluateDraftStatus(bounds, ValidateFloatBounds, ParseFloatDraft);
}

b8 FloatEditModel::setValue(const f64 value){
    return IsFinite(value) && assignAuthoritativeValue(value);
}

NumericEditResult FloatEditModel::commit(const FloatBounds& bounds, const b8 canonical){
    return commitDraftWith(
        bounds,
        canonical,
        ValidateFloatBounds,
        ParseFloatDraft,
        [](const f64 candidate, const f64 value)noexcept{ return BitCast<u64>(candidate) != BitCast<u64>(value); }
    );
}

Expected<b8, ErrorCode> FloatEditModel::canonicalize(const f64 value){
    return canonicalizeWith(value, [](const f64 candidate, const Span<char> buffer)noexcept{ return FormatF64(candidate, buffer); });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

