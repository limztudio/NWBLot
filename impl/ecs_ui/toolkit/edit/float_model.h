// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "numeric_model_operations.h"

#include <global/bit.h>
#include <global/numeric_text.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Committed doubles are finite; signed zero and every finite value survive canonical text roundtrips.
class FloatEditModel final : public NumericEditModelBase<f64, FloatBounds, FloatEditModel>{
    friend class NumericEditModelBase<f64, FloatBounds, FloatEditModel>;


public:
    explicit FloatEditModel(Core::Alloc::GlobalArena& arena)
        : NumericEditModelBase(arena)
    {
        initializeDefault(0.0);
    }


public:
    [[nodiscard]] NumericParseStatus::Enum status(const FloatBounds& bounds = {})const noexcept{
        return EvaluateFloatDraftStatus(m_draft.text(), bounds);
    }
    // Nonfinite values fail atomically. Successful external replacements always fence pending input.
    [[nodiscard]] b8 setValue(const f64 value){
        return IsFinite(value) && assignAuthoritativeValue(value);
    }


private:
    [[nodiscard]] NumericEditResult commit(const FloatBounds& bounds, const b8 canonical){
        return commitDraftWith(
            bounds,
            canonical,
            ValidateFloatBounds,
            ParseFloatDraft,
            DifferentF64Bits
        );
    }
    [[nodiscard]] Expected<b8, ErrorCode> canonicalize(const f64 value){
        return canonicalizeWith(value, [](const f64 candidate, const Span<char> buffer)noexcept{ return FormatF64(candidate, buffer); });
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

