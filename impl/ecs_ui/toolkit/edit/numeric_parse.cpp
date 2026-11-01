// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "numeric_parse.h"

#include <global/simplemath.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_numeric_parse{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Value, typename ValidateGrammar, typename ParseValue, typename AcceptValue>
[[nodiscard]] static Expected<Value, NumericParseStatus::Enum> ParseNumericDraft(
    const AStringView text,
    ValidateGrammar validateGrammar,
    ParseValue parseValue,
    AcceptValue acceptValue
)noexcept{
    const AStringView trimmed = TrimView(text);
    const NumericParseStatus::Enum status = validateGrammar(trimmed);
    if(status != NumericParseStatus::Complete)
        return MakeUnexpected(status);
    const AStringView number = trimmed.front() == '+' ? trimmed.substr(1u) : trimmed;
    const auto value = parseValue(number);
    if(!value || !acceptValue(*value))
        return MakeUnexpected(NumericParseStatus::OutOfRange);
    return *value;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<i64, NumericParseStatus::Enum> ParseIntegerDraft(const AStringView text)noexcept{
    return __hidden_ui_numeric_parse::ParseNumericDraft<i64>(
        text,
        ValidateIntegerGrammar,
        [](const AStringView number)noexcept{ return ParseI64(number); },
        [](const i64)noexcept{ return true; }
    );
}

Expected<f64, NumericParseStatus::Enum> ParseFloatDraft(const AStringView text)noexcept{
    return __hidden_ui_numeric_parse::ParseNumericDraft<f64>(
        text,
        ValidateFloatGrammar,
        [](const AStringView number)noexcept{ return ParseF64FromChars(number); },
        [](const f64 value)noexcept{ return IsFinite(value); }
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

