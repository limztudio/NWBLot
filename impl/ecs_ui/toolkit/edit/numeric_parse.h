// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <global/limit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_NumericEditMaxBytes = 128u;

namespace NumericParseStatus{
    enum Enum : u8{ Complete, Incomplete, Invalid, OutOfRange };
};

namespace NumericBoundsPolicy{
    enum Enum : u8{ Reject, Clamp };
};

struct IntegerBounds{
    i64 minimum = Limit<i64>::s_Min;
    i64 maximum = Limit<i64>::s_Max;
    NumericBoundsPolicy::Enum policy = NumericBoundsPolicy::Reject;
};

struct FloatBounds{
    f64 minimum = Limit<f64>::s_Min;
    f64 maximum = Limit<f64>::s_Max;
    NumericBoundsPolicy::Enum policy = NumericBoundsPolicy::Reject;
};

struct NumericEditResult{
    bool valid = false;
    bool committed = false;
    bool valueChanged = false;
    bool cancelled = false;
    bool rejected = false;
    bool clamped = false;
    bool restored = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool ValidateIntegerBounds(const IntegerBounds& bounds)noexcept;
[[nodiscard]] bool ValidateFloatBounds(const FloatBounds& bounds)noexcept;
// ASCII edge whitespace is ignored. Conversion writes output only for a complete, representable value.
[[nodiscard]] NumericParseStatus::Enum ParseIntegerDraft(AStringView text, i64& output)noexcept;
[[nodiscard]] NumericParseStatus::Enum ParseFloatDraft(AStringView text, f64& output)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

