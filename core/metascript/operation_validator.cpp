// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "operation_validator.h"

#include "arena_names.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_metascript_operation_validator{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool BinaryI64Overflows(const TokenType::Enum op, const i64 lhs, const i64 rhs)noexcept{
    switch(op){
    case TokenType::Plus:
    case TokenType::PlusEqual:
        return AddOverflows<i64>(lhs, rhs);
    case TokenType::Minus:
    case TokenType::MinusEqual:
        return SubtractOverflows<i64>(lhs, rhs);
    case TokenType::Star:
    case TokenType::StarEqual:
        return MultiplyOverflows<i64>(lhs, rhs);
    case TokenType::Slash:
    case TokenType::SlashEqual:
        return DivideOverflows<i64>(lhs, rhs);
    default:
        break;
    }

    return false;
}

[[nodiscard]] bool IsZero(const Value& value)noexcept{
    if(value.isInteger())
        return value.asInteger() == 0;
    if(value.isDouble())
        return value.asDouble() == 0.0;
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool OperationValidator::validateBinaryOperation(const TokenType::Enum op, const Value& lhs, const Value& rhs, const u32 line, const u32 column){
    switch(op){
    case TokenType::Plus:
        if(lhs.isNumeric() && rhs.isNumeric())
            return validateNumericOperands(op, lhs, rhs, line, column, {});
        if(lhs.isString() && rhs.isString()){
            if(AddOverflows<usize>(lhs.asString().size(), rhs.asString().size())){
                m_cursor.error(line, column, MetascriptArenaScope::s_StringConcatenationSizeOverflowText);
                return false;
            }
            return true;
        }
        if(lhs.isList() && rhs.isList()){
            if(AddOverflows<usize>(lhs.asList().size(), rhs.asList().size())){
                m_cursor.error(line, column, MetascriptArenaScope::s_ListConcatenationSizeOverflowText);
                return false;
            }
            return true;
        }
        m_cursor.error(line, column, "operator '+' requires numeric, string, or list operands of matching types");
        return false;
    case TokenType::Minus:
        return validateNumericOperands(op, lhs, rhs, line, column, "operator '-' requires numeric operands");
    case TokenType::Star:
        return validateNumericOperands(op, lhs, rhs, line, column, "operator '*' requires numeric operands");
    case TokenType::Slash:
        return validateNumericOperands(op, lhs, rhs, line, column, "operator '/' requires numeric operands", true);
    default:
        break;
    }

    m_cursor.error(line, column, "unsupported binary operator");
    return false;
}

bool OperationValidator::validateAssignment(const TokenType::Enum op, const Value& target, const Value& rhs, const u32 line, const u32 column){
    switch(op){
    case TokenType::Equal:
        return true;
    case TokenType::PlusEqual:
        if(target.isNumeric() && rhs.isNumeric())
            return validateNumericOperands(op, target, rhs, line, column, {});
        if(target.isString() && rhs.isString()){
            if(AddOverflows<usize>(target.asString().size(), rhs.asString().size())){
                m_cursor.error(line, column, MetascriptArenaScope::s_StringAppendSizeOverflowText);
                return false;
            }
            return true;
        }
        if(target.isList()){
            const usize addedElements = rhs.isList() ? rhs.asList().size() : 1u;
            if(AddOverflows<usize>(target.asList().size(), addedElements)){
                m_cursor.error(line, column, MetascriptArenaScope::s_ListAppendSizeOverflowText);
                return false;
            }
            return true;
        }
        m_cursor.error(line, column, "operator '+=' requires numeric operands, string += string, or a list target");
        return false;
    case TokenType::MinusEqual:
        return validateNumericOperands(op, target, rhs, line, column, "operator '-=' requires numeric operands");
    case TokenType::StarEqual:
        return validateNumericOperands(op, target, rhs, line, column, "operator '*=' requires numeric operands");
    case TokenType::SlashEqual:
        return validateNumericOperands(op, target, rhs, line, column, "operator '/=' requires numeric operands", true);
    default:
        break;
    }

    m_cursor.error(line, column, "unsupported assignment operator");
    return false;
}


bool OperationValidator::validateNumericOperands(const TokenType::Enum op, const Value& lhs, const Value& rhs, const u32 line, const u32 column, const AStringView operandError, const bool rejectZeroDivisor){
    using namespace __hidden_metascript_operation_validator;

    if(!(lhs.isNumeric() && rhs.isNumeric())){
        if(!operandError.empty())
            m_cursor.error(line, column, operandError);
        return false;
    }
    if(rejectZeroDivisor && IsZero(rhs)){
        m_cursor.error(line, column, MetascriptArenaScope::s_DivisionByZeroText);
        return false;
    }
    if(lhs.isInteger() && rhs.isInteger() && BinaryI64Overflows(op, lhs.asInteger(), rhs.asInteger())){
        m_cursor.error(line, column, MetascriptArenaScope::s_IntegerOverflowText);
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

