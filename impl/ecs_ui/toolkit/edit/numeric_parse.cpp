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


[[nodiscard]] static bool IsDigit(const char value){ return value >= '0' && value <= '9'; }

[[nodiscard]] static usize SkipSign(const AStringView text){
    return !text.empty() && (text.front() == '+' || text.front() == '-') ? 1u : 0u;
}

[[nodiscard]] static NumericParseStatus::Enum IntegerGrammar(const AStringView text){
    const usize begin = SkipSign(text);
    if(begin == text.size())
        return NumericParseStatus::Incomplete;
    for(usize index = begin; index < text.size(); ++index){
        if(!IsDigit(text[index]))
            return NumericParseStatus::Invalid;
    }
    return NumericParseStatus::Complete;
}

[[nodiscard]] static NumericParseStatus::Enum FloatGrammar(const AStringView text){
    usize index = SkipSign(text);
    bool digits = false;
    while(index < text.size() && IsDigit(text[index])){
        digits = true;
        ++index;
    }
    if(index < text.size() && text[index] == '.'){
        ++index;
        while(index < text.size() && IsDigit(text[index])){
            digits = true;
            ++index;
        }
    }
    if(!digits)
        return index == text.size() ? NumericParseStatus::Incomplete : NumericParseStatus::Invalid;
    if(index < text.size() && (text[index] == 'e' || text[index] == 'E')){
        ++index;
        if(index < text.size() && (text[index] == '+' || text[index] == '-'))
            ++index;
        const usize exponent = index;
        while(index < text.size() && IsDigit(text[index]))
            ++index;
        if(exponent == index)
            return index == text.size() ? NumericParseStatus::Incomplete : NumericParseStatus::Invalid;
    }
    return index == text.size() ? NumericParseStatus::Complete : NumericParseStatus::Invalid;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ValidateIntegerBounds(const IntegerBounds& bounds){
    return
        bounds.minimum <= bounds.maximum
        && (bounds.policy == NumericBoundsPolicy::Reject || bounds.policy == NumericBoundsPolicy::Clamp)
    ;
}

bool ValidateFloatBounds(const FloatBounds& bounds){
    return
        IsFinite(bounds.minimum) && IsFinite(bounds.maximum) && bounds.minimum <= bounds.maximum
        && (bounds.policy == NumericBoundsPolicy::Reject || bounds.policy == NumericBoundsPolicy::Clamp)
    ;
}

NumericParseStatus::Enum ParseIntegerDraft(const AStringView text, i64& output){
    const AStringView trimmed = TrimView(text);
    const NumericParseStatus::Enum status = __hidden_ui_numeric_parse::IntegerGrammar(trimmed);
    if(status != NumericParseStatus::Complete)
        return status;
    const AStringView number = trimmed.front() == '+' ? trimmed.substr(1u) : trimmed;
    i64 value = 0;
    if(!ParseI64FromChars(number, value))
        return NumericParseStatus::OutOfRange;
    output = value;
    return NumericParseStatus::Complete;
}

NumericParseStatus::Enum ParseFloatDraft(const AStringView text, f64& output){
    const AStringView trimmed = TrimView(text);
    const NumericParseStatus::Enum status = __hidden_ui_numeric_parse::FloatGrammar(trimmed);
    if(status != NumericParseStatus::Complete)
        return status;
    const AStringView number = trimmed.front() == '+' ? trimmed.substr(1u) : trimmed;
    f64 value = 0.0;
    if(!ParseF64FromChars(number, value) || !IsFinite(value))
        return NumericParseStatus::OutOfRange;
    output = value;
    return NumericParseStatus::Complete;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

