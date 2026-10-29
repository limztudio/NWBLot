// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <global/limit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_NumericEditMaxBytes = 128u;
inline constexpr usize s_NumericEditMaxHistoryRecords = 64u;
inline constexpr usize s_NumericEditHistoryBytesFactor = 128u;

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
    b8 valid = false;
    b8 committed = false;
    b8 valueChanged = false;
    b8 cancelled = false;
    b8 rejected = false;
    b8 clamped = false;
    b8 restored = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 ValidateIntegerBounds(const IntegerBounds& bounds)noexcept;
[[nodiscard]] b8 ValidateFloatBounds(const FloatBounds& bounds)noexcept;
// ASCII edge whitespace is ignored; incomplete and malformed drafts retain their classification.
[[nodiscard]] Expected<i64, NumericParseStatus::Enum> ParseIntegerDraft(AStringView text)noexcept;
[[nodiscard]] Expected<f64, NumericParseStatus::Enum> ParseFloatDraft(AStringView text)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

