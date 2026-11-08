// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "ast_common.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class OperatorSemantics final{
public:
    [[nodiscard]] static constexpr b8 IsOverloadableOperatorToken(const TokenType::Enum op)noexcept{
    #define NWB_ADHD_EXPORT_OPERATOR_TOKEN(tokenType, symbol, isOverloadable, isAssignment, returnsBool, isBitwiseFlagOp, isPrefixUnary, isIncDec, binaryPrecedence, minMemberParams, maxMemberParams, prohibitsUnaryOverload) \
        if constexpr(isOverloadable){ \
            if(op == tokenType) return true; \
        }
    #include "../export_operators.h"
    #undef NWB_ADHD_EXPORT_OPERATOR_TOKEN
        return false;
    }

    [[nodiscard]] static constexpr b8 IsPrefixUnaryOperatorToken(const TokenType::Enum op)noexcept{
    #define NWB_ADHD_EXPORT_OPERATOR_TOKEN(tokenType, symbol, isOverloadable, isAssignment, returnsBool, isBitwiseFlagOp, isPrefixUnary, isIncDec, binaryPrecedence, minMemberParams, maxMemberParams, prohibitsUnaryOverload) \
        if constexpr(isPrefixUnary){ \
            if(op == tokenType) return true; \
        }
    #include "../export_operators.h"
    #undef NWB_ADHD_EXPORT_OPERATOR_TOKEN
        return false;
    }

    [[nodiscard]] static constexpr b8 IsIncrementOrDecrementOperatorToken(const TokenType::Enum op)noexcept{
    #define NWB_ADHD_EXPORT_OPERATOR_TOKEN(tokenType, symbol, isOverloadable, isAssignment, returnsBool, isBitwiseFlagOp, isPrefixUnary, isIncDec, binaryPrecedence, minMemberParams, maxMemberParams, prohibitsUnaryOverload) \
        if constexpr(isIncDec){ \
            if(op == tokenType) return true; \
        }
    #include "../export_operators.h"
    #undef NWB_ADHD_EXPORT_OPERATOR_TOKEN
        return false;
    }

    [[nodiscard]] static constexpr u8 BinaryOperatorPrecedence(const TokenType::Enum op)noexcept{
    #define NWB_ADHD_EXPORT_OPERATOR_TOKEN(tokenType, symbol, isOverloadable, isAssignment, returnsBool, isBitwiseFlagOp, isPrefixUnary, isIncDec, binaryPrecedence, minMemberParams, maxMemberParams, prohibitsUnaryOverload) \
        if constexpr(binaryPrecedence > 0u){ \
            if(op == tokenType) return binaryPrecedence; \
        }
    #include "../export_operators.h"
    #undef NWB_ADHD_EXPORT_OPERATOR_TOKEN
        return 0u;
    }

    [[nodiscard]] static constexpr MStringView FindProhibitedUnaryOperatorError(const TokenType::Enum op)noexcept{
    #define NWB_ADHD_EXPORT_PROHIBITED_UNARY_OP(tokenType, errorMessage) \
        if(op == tokenType) return errorMessage;
    #include "../export_operators.h"
    #undef NWB_ADHD_EXPORT_PROHIBITED_UNARY_OP
        return {};
    }

    [[nodiscard]] static constexpr MStringView FindProhibitedOperatorOverloadError(const TokenType::Enum op)noexcept{
    #define NWB_ADHD_EXPORT_PROHIBITED_OPERATOR_OVERLOAD(tokenType, errorMessage) \
        if(op == tokenType) return errorMessage;
    #include "../export_operators.h"
    #undef NWB_ADHD_EXPORT_PROHIBITED_OPERATOR_OVERLOAD
        return {};
    }

    [[nodiscard]] static constexpr b8 TryGetMemberOperatorArityRules(const MStringView opSym, usize& outMinParams, usize& outMaxParams, b8& outProhibitsUnary)noexcept{
        if(opSym == "[]"){
            outMinParams = 1u;
            outMaxParams = 1u;
            outProhibitsUnary = false;
            return true;
        }
    #define NWB_ADHD_EXPORT_OPERATOR_TOKEN(tokenType, symbol, isOverloadable, isAssignment, returnsBool, isBitwiseFlagOp, isPrefixUnary, isIncDec, binaryPrecedence, minMemberParams, maxMemberParams, prohibitsUnaryOverload) \
        if constexpr(isOverloadable){ \
            if(opSym == symbol){ \
                outMinParams = minMemberParams; \
                outMaxParams = maxMemberParams; \
                outProhibitsUnary = prohibitsUnaryOverload; \
                return true; \
            } \
        }
    #include "../export_operators.h"
    #undef NWB_ADHD_EXPORT_OPERATOR_TOKEN
        return false;
    }

    [[nodiscard]] static constexpr b8 IsCompoundOrSimpleAssignment(const TokenType::Enum op)noexcept{
    #define NWB_ADHD_EXPORT_OPERATOR_TOKEN(tokenType, symbol, isOverloadable, isAssignment, returnsBool, isBitwiseFlagOp, isPrefixUnary, isIncDec, binaryPrecedence, minMemberParams, maxMemberParams, prohibitsUnaryOverload) \
        if constexpr(isAssignment){ \
            if(op == tokenType) return true; \
        }
    #include "../export_operators.h"
    #undef NWB_ADHD_EXPORT_OPERATOR_TOKEN
        return false;
    }

    [[nodiscard]] static constexpr b8 IsBoolReturnOperatorToken(const TokenType::Enum op)noexcept{
    #define NWB_ADHD_EXPORT_OPERATOR_TOKEN(tokenType, symbol, isOverloadable, isAssignment, returnsBool, isBitwiseFlagOp, isPrefixUnary, isIncDec, binaryPrecedence, minMemberParams, maxMemberParams, prohibitsUnaryOverload) \
        if constexpr(returnsBool){ \
            if(op == tokenType) return true; \
        }
    #include "../export_operators.h"
    #undef NWB_ADHD_EXPORT_OPERATOR_TOKEN
        return false;
    }

    [[nodiscard]] static constexpr b8 IsBoolReturnOperatorSymbol(const MStringView sym)noexcept{
    #define NWB_ADHD_EXPORT_OPERATOR_TOKEN(tokenType, symbol, isOverloadable, isAssignment, returnsBool, isBitwiseFlagOp, isPrefixUnary, isIncDec, binaryPrecedence, minMemberParams, maxMemberParams, prohibitsUnaryOverload) \
        if constexpr(returnsBool){ \
            if(sym == symbol) return true; \
        }
    #include "../export_operators.h"
    #undef NWB_ADHD_EXPORT_OPERATOR_TOKEN
        return false;
    }

    [[nodiscard]] static constexpr b8 IsBitwiseFlagOperatorToken(const TokenType::Enum op)noexcept{
    #define NWB_ADHD_EXPORT_OPERATOR_TOKEN(tokenType, symbol, isOverloadable, isAssignment, returnsBool, isBitwiseFlagOp, isPrefixUnary, isIncDec, binaryPrecedence, minMemberParams, maxMemberParams, prohibitsUnaryOverload) \
        if constexpr(isBitwiseFlagOp){ \
            if(op == tokenType) return true; \
        }
    #include "../export_operators.h"
    #undef NWB_ADHD_EXPORT_OPERATOR_TOKEN
        return false;
    }

    [[nodiscard]] static constexpr b8 IsArithmeticOrShiftAssignOperatorToken(const TokenType::Enum op)noexcept{
    #define NWB_ADHD_EXPORT_OPERATOR_TOKEN(tokenType, symbol, isOverloadable, isAssignment, returnsBool, isBitwiseFlagOp, isPrefixUnary, isIncDec, binaryPrecedence, minMemberParams, maxMemberParams, prohibitsUnaryOverload) \
        if constexpr(isAssignment && !isBitwiseFlagOp && tokenType != TokenType::Equal){ \
            if(op == tokenType) return true; \
        }
    #include "../export_operators.h"
    #undef NWB_ADHD_EXPORT_OPERATOR_TOKEN
        return false;
    }

    [[nodiscard]] static constexpr MStringView MapTokenTypeToOperatorSymbol(const TokenType::Enum op)noexcept{
    #define NWB_ADHD_EXPORT_OPERATOR_TOKEN(tokenType, symbol, isOverloadable, isAssignment, returnsBool, isBitwiseFlagOp, isPrefixUnary, isIncDec, binaryPrecedence, minMemberParams, maxMemberParams, prohibitsUnaryOverload) \
        if(op == tokenType) return symbol;
    #include "../export_operators.h"
    #undef NWB_ADHD_EXPORT_OPERATOR_TOKEN
        return {};
    }

    [[nodiscard]] static constexpr TokenType::Enum MapOperatorSymbolToTokenType(const MStringView sym)noexcept{
    #define NWB_ADHD_EXPORT_OPERATOR_TOKEN(tokenType, symbol, isOverloadable, isAssignment, returnsBool, isBitwiseFlagOp, isPrefixUnary, isIncDec, binaryPrecedence, minMemberParams, maxMemberParams, prohibitsUnaryOverload) \
        if(sym == symbol) return tokenType;
    #include "../export_operators.h"
    #undef NWB_ADHD_EXPORT_OPERATOR_TOKEN
        return TokenType::EndOfFile;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


