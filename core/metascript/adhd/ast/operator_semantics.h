// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "ast_common.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct BracketOperatorInfo{
    TokenType::Enum openToken;
    TokenType::Enum closeToken;
    MStringView symbol;
    usize minMemberParams;
    usize maxMemberParams;
    MStringView missingCloseError;
};

struct OperatorTokenInfo{
    TokenType::Enum token;
    MStringView symbol;
    u8 precedence;
    usize minMemberParams;
    usize maxMemberParams;
    b8 isOverloadable;
    b8 isAssignment;
    b8 returnsBool;
    b8 isBitwiseFlagOp;
    b8 isPrefixUnary;
    b8 isIncDec;
    b8 prohibitsUnaryOverload;
};


class OperatorSemantics final{
public:
    inline static constexpr auto s_BracketOperators = Array{
    #define NWB_ADHD_EXPORT_BRACKET_OPERATOR(openToken, closeToken, symbol, minMemberParams, maxMemberParams, missingCloseError) \
        BracketOperatorInfo{ openToken, closeToken, symbol, minMemberParams, maxMemberParams, missingCloseError },
    #include "../export_operators.h"
    #undef NWB_ADHD_EXPORT_BRACKET_OPERATOR
    };

    inline static constexpr MStringView s_SubscriptSymbol = s_BracketOperators[0].symbol;

    inline static constexpr auto s_OperatorTokens = Array{
    #define NWB_ADHD_EXPORT_OPERATOR_TOKEN(tokenType, symbol, isOverloadable, isAssignment, returnsBool, isBitwiseFlagOp, isPrefixUnary, isIncDec, binaryPrecedence, minMemberParams, maxMemberParams, prohibitsUnaryOverload) \
        OperatorTokenInfo{ tokenType, symbol, binaryPrecedence, minMemberParams, maxMemberParams, isOverloadable, isAssignment, returnsBool, isBitwiseFlagOp, isPrefixUnary, isIncDec, prohibitsUnaryOverload },
    #include "../export_operators.h"
    #undef NWB_ADHD_EXPORT_OPERATOR_TOKEN
    };


public:
    [[nodiscard]] static constexpr const OperatorTokenInfo* FindOperatorToken(const TokenType::Enum op)noexcept{
        for(const OperatorTokenInfo& entry : s_OperatorTokens){
            if(entry.token == op)
                return &entry;
        }
        return nullptr;
    }

    [[nodiscard]] static constexpr const OperatorTokenInfo* FindOperatorBySymbol(const MStringView sym)noexcept{
        for(const OperatorTokenInfo& entry : s_OperatorTokens){
            if(entry.symbol == sym)
                return &entry;
        }
        return nullptr;
    }

    [[nodiscard]] static constexpr b8 IsOverloadableOperatorToken(const TokenType::Enum op)noexcept{
        const OperatorTokenInfo* entry = FindOperatorToken(op);
        return entry && entry->isOverloadable;
    }

    [[nodiscard]] static constexpr b8 IsPrefixUnaryOperatorToken(const TokenType::Enum op)noexcept{
        const OperatorTokenInfo* entry = FindOperatorToken(op);
        return entry && entry->isPrefixUnary;
    }

    [[nodiscard]] static constexpr b8 IsIncrementOrDecrementOperatorToken(const TokenType::Enum op)noexcept{
        const OperatorTokenInfo* entry = FindOperatorToken(op);
        return entry && entry->isIncDec;
    }

    [[nodiscard]] static constexpr u8 BinaryOperatorPrecedence(const TokenType::Enum op)noexcept{
        const OperatorTokenInfo* entry = FindOperatorToken(op);
        return entry ? entry->precedence : 0u;
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
        for(const BracketOperatorInfo& bracketOp : s_BracketOperators){
            if(opSym == bracketOp.symbol){
                outMinParams = bracketOp.minMemberParams;
                outMaxParams = bracketOp.maxMemberParams;
                outProhibitsUnary = false;
                return true;
            }
        }
        const OperatorTokenInfo* entry = FindOperatorBySymbol(opSym);
        if(entry && entry->isOverloadable){
            outMinParams = entry->minMemberParams;
            outMaxParams = entry->maxMemberParams;
            outProhibitsUnary = entry->prohibitsUnaryOverload;
            return true;
        }
        return false;
    }

    [[nodiscard]] static constexpr b8 ValidateMemberOperatorArity(const MStringView opSym, const usize paramCount, MStringView& outErrorMsg)noexcept{
        usize minParams = 1u;
        usize maxParams = 1u;
        b8 prohibitsUnary = false;
        if(!TryGetMemberOperatorArityRules(opSym, minParams, maxParams, prohibitsUnary))
            return false;
    #define NWB_ADHD_EXPORT_MEMBER_OPERATOR_ARITY_ERROR(prohibitsUnaryZeroMsg, zeroMaxParamsMsg, optionalParamMsg, binaryOrIndexMsg) \
        if(prohibitsUnary && paramCount == 0u){ \
            outErrorMsg = prohibitsUnaryZeroMsg; \
            return false; \
        } \
        if(paramCount < minParams || paramCount > maxParams){ \
            if(maxParams == 0u) \
                outErrorMsg = zeroMaxParamsMsg; \
            else if(minParams == 0u) \
                outErrorMsg = optionalParamMsg; \
            else \
                outErrorMsg = binaryOrIndexMsg; \
            return false; \
        }
    #include "../export_operators.h"
    #undef NWB_ADHD_EXPORT_MEMBER_OPERATOR_ARITY_ERROR
        return true;
    }

    [[nodiscard]] static constexpr b8 TryGetBracketOperator(const TokenType::Enum openTok, TokenType::Enum& outCloseTok, MStringView& outSymbol, MStringView& outMissingCloseErr)noexcept{
        for(const BracketOperatorInfo& bracketOp : s_BracketOperators){
            if(bracketOp.openToken == openTok){
                outCloseTok = bracketOp.closeToken;
                outSymbol = bracketOp.symbol;
                outMissingCloseErr = bracketOp.missingCloseError;
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] static constexpr MStringView InternOverloadableOperatorSymbol(const MStringView sym)noexcept{
        for(const BracketOperatorInfo& bracketOp : s_BracketOperators){
            if(sym == bracketOp.symbol)
                return bracketOp.symbol;
        }
        const OperatorTokenInfo* entry = FindOperatorBySymbol(sym);
        return (entry && entry->isOverloadable) ? entry->symbol : MStringView{};
    }

    [[nodiscard]] static constexpr b8 IsCompoundOrSimpleAssignment(const TokenType::Enum op)noexcept{
        const OperatorTokenInfo* entry = FindOperatorToken(op);
        return entry && entry->isAssignment;
    }

    [[nodiscard]] static constexpr b8 IsBoolReturnOperatorToken(const TokenType::Enum op)noexcept{
        const OperatorTokenInfo* entry = FindOperatorToken(op);
        return entry && entry->returnsBool;
    }

    [[nodiscard]] static constexpr b8 IsBoolReturnOperatorSymbol(const MStringView sym)noexcept{
        const OperatorTokenInfo* entry = FindOperatorBySymbol(sym);
        return entry && entry->returnsBool;
    }

    [[nodiscard]] static constexpr b8 IsRelationalOrEqualityOperatorToken(const TokenType::Enum op)noexcept{
        const OperatorTokenInfo* entry = FindOperatorToken(op);
        return entry && entry->returnsBool && entry->precedence >= 6u;
    }

    [[nodiscard]] static constexpr b8 IsBitwiseFlagOperatorToken(const TokenType::Enum op)noexcept{
        const OperatorTokenInfo* entry = FindOperatorToken(op);
        return entry && entry->isBitwiseFlagOp;
    }

    [[nodiscard]] static constexpr b8 IsArithmeticOrShiftAssignOperatorToken(const TokenType::Enum op)noexcept{
        const OperatorTokenInfo* entry = FindOperatorToken(op);
        return entry && entry->isAssignment && !entry->isBitwiseFlagOp && entry->token != TokenType::Equal;
    }

    template<typename EmitFn>
    static void ForEachBitwiseFlagOperator(EmitFn&& emit){
        for(const OperatorTokenInfo& entry : s_OperatorTokens){
            if(entry.isBitwiseFlagOp && !entry.isAssignment)
                emit(entry.symbol);
        }
    }

    [[nodiscard]] static constexpr MStringView MapTokenTypeToOperatorSymbol(const TokenType::Enum op)noexcept{
        const OperatorTokenInfo* entry = FindOperatorToken(op);
        return entry ? entry->symbol : MStringView{};
    }

    [[nodiscard]] static constexpr TokenType::Enum MapOperatorSymbolToTokenType(const MStringView sym)noexcept{
        const OperatorTokenInfo* entry = FindOperatorBySymbol(sym);
        return entry ? entry->token : TokenType::EndOfFile;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

