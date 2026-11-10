// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/metascript/literal_decoder.h>

#include <core/metascript/adhd/ast/keyword_semantics.h>

#include <core/metascript/adhd/ast/builtin_type_semantics.h>

#include <core/metascript/adhd/ast/operator_semantics.h>

#include "const_eval.h"

#include <global/source_text.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_const_eval{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr i64 s_ShiftOperandMin = 0;
constexpr i64 s_I64ShiftOperandMax = 64;


[[nodiscard]] constexpr b8 TryEvaluateIntegerScalarLimit(const MStringView scalarType, const MStringView member, i64& outVal)noexcept{
    b8 isMin = false;
    if(!BuiltinTypeSemantics::TryMapScalarLimitMember(member, nullptr, &isMin))
        return false;
#define NWB_ADHD_EXPORT_SCALAR_TYPE(adhdName, cppType, byteSize, byteAlign, isInteger, isFixedWidthInt, isSigned, isFloat) \
    if constexpr(isInteger){ \
        if(scalarType == adhdName){ \
            outVal = isMin ? static_cast<i64>(Limit<::cppType>::s_Min) : static_cast<i64>(Limit<::cppType>::s_Max); \
            return true; \
        } \
    }
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_SCALAR_TYPE
    return false;
}

[[nodiscard]] b8 IsValidConstTemplateArgGlobal(const Stmt* globalConst, const SymbolTable& symbols)noexcept{
    if(!globalConst || !globalConst->varType)
        return false;
    const MStringView baseName = symbols.resolveBaseTypeName(globalConst->varType->name);
    return BuiltinTypeSemantics::IsIntegralOrBoolScalarType(baseName) || symbols.findEnum(baseName) != nullptr || FindBuiltinEnum(baseName) != nullptr;
}


[[nodiscard]] constexpr b8 TryApplyUnaryConstantOp(const TokenType::Enum op, const i64 inner, i64& outVal)noexcept{
    switch(op){
    case TokenType::Plus: outVal = +inner; return true;
    case TokenType::Minus: outVal = -inner; return true;
    case TokenType::Tilde: outVal = ~inner; return true;
    case TokenType::Bang: outVal = (inner == 0) ? 1 : 0; return true;
    default: return false;
    }
}

[[nodiscard]] constexpr b8 TryApplyBinaryConstantOp(const TokenType::Enum op, const i64 lhsVal, const i64 rhsVal, i64& outVal)noexcept{
    switch(op){
    case TokenType::Plus: outVal = lhsVal + rhsVal; return true;
    case TokenType::Minus: outVal = lhsVal - rhsVal; return true;
    case TokenType::Star: outVal = lhsVal * rhsVal; return true;
    case TokenType::Slash:
        if(rhsVal == 0 || (lhsVal == Limit<i64>::s_Min && rhsVal == -1))
            return false;
        outVal = lhsVal / rhsVal;
        return true;
    case TokenType::Percent:
        if(rhsVal == 0)
            return false;
        outVal = (lhsVal == Limit<i64>::s_Min && rhsVal == -1) ? 0 : (lhsVal % rhsVal);
        return true;
    case TokenType::LessLess:
        if(rhsVal < s_ShiftOperandMin || rhsVal >= s_I64ShiftOperandMax)
            return false;
        outVal = static_cast<i64>(static_cast<u64>(lhsVal) << static_cast<u64>(rhsVal));
        return true;
    case TokenType::GreaterGreater:
        if(rhsVal < s_ShiftOperandMin || rhsVal >= s_I64ShiftOperandMax)
            return false;
        outVal = lhsVal >> rhsVal;
        return true;
    case TokenType::Pipe: outVal = lhsVal | rhsVal; return true;
    case TokenType::Caret: outVal = lhsVal ^ rhsVal; return true;
    case TokenType::Ampersand: outVal = lhsVal & rhsVal; return true;
    default: return false;
    }
}

struct ActiveEnumEvalContext{
    const EnumDecl* enumDecl = nullptr;
    const i64* evaluatedValues = nullptr;
    usize evaluatedCount = 0u;
};

inline thread_local const ActiveEnumEvalContext* s_ActiveEnumCtx = nullptr;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 IsValidScopedEnumTemplateArg(MStringView text, const SymbolTable& symbols, const EnumDecl** outEnum, MStringView* outEntryName)noexcept{
    text = TrimView(text);
    const usize firstSep = text.find("::");
    if(firstSep == MStringView::npos || firstSep == 0u)
        return false;
    const MStringView enumName = TrimView(text.substr(0u, firstSep));
    const EnumDecl* ed = symbols.findEnum(symbols.resolveBaseTypeName(enumName));
    if(!ed)
        return false;

    const MStringView entryName = TrimView(text.substr(firstSep + 2u));
    if(entryName.empty() || entryName.find("::") != MStringView::npos)
        return false;

    for(const EnumEntry& entry : ed->entries){
        if(entry.name == entryName){
            if(outEnum)
                *outEnum = ed;
            if(outEntryName)
                *outEntryName = entryName;
            return true;
        }
    }
    return false;
}

b8 TryEvaluateEnumEntryConstant(const EnumDecl& ed, const MStringView targetEntry, const SymbolTable& symbols, const u32 depth, i64& outVal)noexcept{
    if(depth > 16u)
        return false;
    if(__hidden_adhd_const_eval::s_ActiveEnumCtx && __hidden_adhd_const_eval::s_ActiveEnumCtx->enumDecl == &ed){
        for(usize k = 0u; k < __hidden_adhd_const_eval::s_ActiveEnumCtx->evaluatedCount; ++k){
            if(ed.entries[k].name == targetEntry){
                outVal = __hidden_adhd_const_eval::s_ActiveEnumCtx->evaluatedValues[k];
                return true;
            }
        }
        return false;
    }

    constexpr usize s_MaxCachedEntries = 256u;
    Array<i64, s_MaxCachedEntries> cachedValues{};
    const __hidden_adhd_const_eval::ActiveEnumEvalContext* prevCtx = __hidden_adhd_const_eval::s_ActiveEnumCtx;
    __hidden_adhd_const_eval::ActiveEnumEvalContext localCtx{ &ed, cachedValues.data(), 0u };

    i64 nextSeq = 0;
    usize flagBitIndex = 0u;
    for(usize idx = 0u; idx < ed.entries.size(); ++idx){
        const EnumEntry& entry = ed.entries[idx];
        i64 curVal = 0;
        if(entry.valueExpr){
            localCtx.evaluatedCount = idx < s_MaxCachedEntries ? idx : s_MaxCachedEntries;
            __hidden_adhd_const_eval::s_ActiveEnumCtx = &localCtx;
            const b8 evalOk = TryEvaluateExprConstant(entry.valueExpr, symbols, depth + 1u, &ed, curVal);
            __hidden_adhd_const_eval::s_ActiveEnumCtx = prevCtx;
            if(!evalOk)
                return false;
            nextSeq = static_cast<i64>(static_cast<u64>(curVal) + 1u);
        }
        else if(ed.isFlag){
            if(flagBitIndex >= __hidden_adhd_const_eval::s_I64ShiftOperandMax)
                return false;
            curVal = static_cast<i64>(u64(1) << flagBitIndex++);
            nextSeq = static_cast<i64>(static_cast<u64>(curVal) + 1u);
        }
        else{
            curVal = nextSeq;
            nextSeq = static_cast<i64>(static_cast<u64>(nextSeq) + 1u);
        }
        if(idx < s_MaxCachedEntries)
            cachedValues[idx] = curVal;
        if(entry.name == targetEntry){
            outVal = curVal;
            return true;
        }
    }
    return false;
}

b8 TryEvaluateExprConstant(const Expr* expr, const SymbolTable& symbols, const u32 depth, const EnumDecl* contextEnum, i64& outVal)noexcept{
    if(!expr || depth > 16u)
        return false;

    switch(expr->kind){
    case ExprKind::IntegerLiteral:
    case ExprKind::Identifier:
        return TryEvaluateTextConstant(expr->text, symbols, depth + 1u, contextEnum, outVal);
    case ExprKind::BoolLiteral:
        return KeywordSemantics::TryParseBoolLiteral(expr->text, &outVal);
    case ExprKind::MemberAccess:{
        if(!expr->lhs || expr->lhs->kind != ExprKind::Identifier || !expr->lhs->callTemplateArgs.empty() || symbols.findGlobalConstant(expr->lhs->text))
            return false;
        const MStringView resolvedBaseType = symbols.resolveBaseTypeName(expr->lhs->text);
        return __hidden_adhd_const_eval::TryEvaluateIntegerScalarLimit(resolvedBaseType, expr->text, outVal);
    }
    case ExprKind::ScopeAccess:{
        if(expr->scopeChain.size() != 2u)
            return false;
        const EnumDecl* ed = symbols.findEnum(symbols.resolveBaseTypeName(expr->scopeChain[0]));
        if(!ed)
            return false;
        return TryEvaluateEnumEntryConstant(*ed, expr->scopeChain[1], symbols, depth, outVal);
    }
    case ExprKind::Unary:{
        if(expr->isPostfix)
            return false;
        i64 inner = 0;
        if(!TryEvaluateExprConstant(expr->lhs, symbols, depth + 1u, contextEnum, inner))
            return false;
        return __hidden_adhd_const_eval::TryApplyUnaryConstantOp(expr->op, inner, outVal);
    }
    case ExprKind::Binary:{
        i64 lhsVal = 0;
        i64 rhsVal = 0;
        if(!TryEvaluateExprConstant(expr->lhs, symbols, depth + 1u, contextEnum, lhsVal) || !TryEvaluateExprConstant(expr->rhs, symbols, depth + 1u, contextEnum, rhsVal))
            return false;
        return __hidden_adhd_const_eval::TryApplyBinaryConstantOp(expr->op, lhsVal, rhsVal, outVal);
    }
    case ExprKind::Cast:
        return TryEvaluateExprConstant(expr->lhs, symbols, depth + 1u, contextEnum, outVal);
    default:
        return false;
    }
}

b8 TryEvaluateTextConstant(MStringView text, const SymbolTable& symbols, const u32 depth, const EnumDecl* contextEnum, i64& outVal)noexcept{
    if(depth > 16u)
        return false;
    text = TrimView(text);
    while(HasMatchingOuterParens(text))
        text = TrimView(text.substr(1u, text.size() - 2u));
    if(text.empty())
        return false;

    const auto findTopLevelBinary = [&](const MStringView op1, const MStringView op2, usize& outPos, usize& outLen)noexcept -> b8{
        u32 parenDepth = 0u;
        b8 inSingleQuote = false;
        usize foundPos = MStringView::npos;
        usize foundLen = 0u;
        for(usize i = 0u; i < text.size(); ++i){
            const MChar c = text[i];
            if(c == '\'' && (i == 0u || text[i - 1u] != '\\'))
                inSingleQuote = !inSingleQuote;
            if(inSingleQuote)
                continue;
            if(c == '('){
                ++parenDepth;
                continue;
            }
            if(c == ')'){
                if(parenDepth > 0u)
                    --parenDepth;
                continue;
            }
            if(parenDepth != 0u)
                continue;

            const auto matchesAt = [&](const MStringView op)noexcept -> b8{
                if(op.empty() || i + op.size() > text.size())
                    return false;
                if(text.substr(i, op.size()) != op)
                    return false;
                if(op.size() == 1u && (op[0] == '<' || op[0] == '>' || op[0] == '|' || op[0] == '&')){
                    if(i + 1u < text.size() && text[i + 1u] == op[0])
                        return false;
                    if(i > 0u && text[i - 1u] == op[0])
                        return false;
                }
                if(op.size() == 1u && (op[0] == '+' || op[0] == '-')){
                    const MStringView prefix = TrimView(text.substr(0u, i));
                    if(prefix.empty())
                        return false;
                    const MChar prev = prefix.back();
                    if(prev == '+' || prev == '-' || prev == '*' || prev == '/' || prev == '%' || prev == '<' || prev == '>' || prev == '|' || prev == '^' || prev == '&' || prev == '~' || prev == '!')
                        return false;
                }
                return true;
            };

            if(matchesAt(op1)){
                foundPos = i;
                foundLen = op1.size();
            }
            else if(!op2.empty() && matchesAt(op2)){
                foundPos = i;
                foundLen = op2.size();
            }
        }
        if(foundPos != MStringView::npos){
            outPos = foundPos;
            outLen = foundLen;
            return true;
        }
        return false;
    };

    const auto evalBinaryOp = [&](const usize pos, const usize len, const TokenType::Enum op)noexcept -> b8{
        i64 l = 0;
        i64 r = 0;
        if(!TryEvaluateTextConstant(text.substr(0u, pos), symbols, depth + 1u, contextEnum, l) || !TryEvaluateTextConstant(text.substr(pos + len), symbols, depth + 1u, contextEnum, r))
            return false;
        return __hidden_adhd_const_eval::TryApplyBinaryConstantOp(op, l, r, outVal);
    };

    usize opPos = 0u;
    usize opLen = 0u;
    constexpr Array<Array<MStringView, 2>, 7> s_ConstBinaryPrecedenceLevels = {{
        { "|", "" },
        { "^", "" },
        { "&", "" },
        { "<<", ">>" },
        { "+", "-" },
        { "*", "/" },
        { "%", "" },
    }};
    for(const auto& level : s_ConstBinaryPrecedenceLevels){
        if(findTopLevelBinary(level[0], level[1], opPos, opLen))
            return evalBinaryOp(opPos, opLen, OperatorSemantics::MapOperatorSymbolToTokenType(text.substr(opPos, opLen)));
    }

    const TokenType::Enum leadingUnaryOp = OperatorSemantics::MapOperatorSymbolToTokenType(text.substr(0u, 1u));
    if(OperatorSemantics::IsPrefixUnaryOperatorToken(leadingUnaryOp) && !OperatorSemantics::IsIncrementOrDecrementOperatorToken(leadingUnaryOp)){
        i64 inner = 0;
        if(!TryEvaluateTextConstant(text.substr(1u), symbols, depth + 1u, contextEnum, inner))
            return false;
        return __hidden_adhd_const_eval::TryApplyUnaryConstantOp(leadingUnaryOp, inner, outVal);
    }

    if(KeywordSemantics::TryParseBoolLiteral(text, &outVal))
        return true;

    if((text.size() >= 3u && text.front() == '\'' && text.back() == '\'') || IsAsciiDigit(text[0])){
        u64 parsedVal = 0u;
        if(!LiteralDecoder::TryParseUnsignedIntegerLiteral(text, parsedVal))
            return false;
        outVal = static_cast<i64>(parsedVal);
        return true;
    }

    const EnumDecl* scopedEnum = nullptr;
    MStringView scopedEntryName;
    if(IsValidScopedEnumTemplateArg(text, symbols, &scopedEnum, &scopedEntryName))
        return TryEvaluateEnumEntryConstant(*scopedEnum, scopedEntryName, symbols, depth, outVal);

    if(const usize dotPos = text.find('.'); dotPos != MStringView::npos && dotPos > 0u){
        const MStringView lhsIdent = TrimView(text.substr(0u, dotPos));
        const MStringView rhsMember = TrimView(text.substr(dotPos + 1u));
        if(!symbols.findGlobalConstant(lhsIdent) && __hidden_adhd_const_eval::TryEvaluateIntegerScalarLimit(symbols.resolveBaseTypeName(lhsIdent), rhsMember, outVal))
            return true;
    }

    if(contextEnum && TryEvaluateEnumEntryConstant(*contextEnum, text, symbols, depth, outVal))
        return true;

    if(const Stmt* globalConst = symbols.findGlobalConstant(text)){
        if(!__hidden_adhd_const_eval::IsValidConstTemplateArgGlobal(globalConst, symbols))
            return false;
        const Expr* valExpr = globalConst->expr;
        if(!valExpr && globalConst->isCtorInit && globalConst->arguments.size() == 1u)
            valExpr = globalConst->arguments[0];
        if(valExpr)
            return TryEvaluateExprConstant(valExpr, symbols, depth + 1u, contextEnum, outVal);
    }

    return false;
}

b8 IsConcreteConstTemplateArgText(const MStringView text, const SymbolTable& symbols)noexcept{
    if(text.empty())
        return false;
    i64 dummyVal = 0;
    if(TryEvaluateTextConstant(text, symbols, dummyVal))
        return true;
    MStringView ident = TrimView(text);
    if(!ident.empty() && (ident.front() == '+' || ident.front() == '-'))
        ident = TrimView(ident.substr(1u));
    return __hidden_adhd_const_eval::IsValidConstTemplateArgGlobal(symbols.findGlobalConstant(ident), symbols);
}

b8 AreConstantTemplateArgsEquivalent(const MStringView a, const MStringView b, const SymbolTable& symbols)noexcept{
    if(a == b)
        return true;
    if(a.empty() || b.empty())
        return false;
    if(KeywordSemantics::IsBoolLiteralText(a) != KeywordSemantics::IsBoolLiteralText(b))
        return false;
    i64 valA = 0;
    i64 valB = 0;
    if(TryEvaluateTextConstant(a, symbols, valA) && TryEvaluateTextConstant(b, symbols, valB))
        return valA == valB;
    return false;
}

b8 HasConstValueTemplateParam(const ClassDecl* ownerClass, const MethodDecl* method, const MStringView name)noexcept{
    if(method){
        if(const TemplateParamDecl* tp = FindTemplateParamByName(method->templateParams, name); tp && tp->constValueType)
            return true;
    }
    if(ownerClass){
        if(const TemplateParamDecl* tp = FindTemplateParamByName(ownerClass->templateParams, name); tp && tp->constValueType)
            return true;
    }
    return false;
}

b8 NormalizeConstantTemplateArg(TypeArg& arg, const ClassDecl* enclosingClass, const MethodDecl* enclosingMethod, const SymbolTable& symbols)noexcept{
    if(!arg.constantText.empty()){
        const MStringView ctext = TrimView(arg.constantText);
        if(IsConcreteConstTemplateArgText(ctext, symbols))
            return true;
        MStringView stripped = ctext;
        if(!stripped.empty() && (stripped.front() == '+' || stripped.front() == '-'))
            stripped = TrimView(stripped.substr(1u));
        return HasConstValueTemplateParam(enclosingClass, enclosingMethod, stripped);
    }
    if(!arg.type || arg.type->refMode != ParamPassingMode::None || !arg.type->templateArgs.empty() || arg.type->isFuncType)
        return false;
    const MStringView name = arg.type->name;
    if(KeywordSemantics::IsBoolLiteralText(name) || IsConcreteConstTemplateArgText(name, symbols) || HasConstValueTemplateParam(enclosingClass, enclosingMethod, name)){
        arg.constantText = name;
        arg.type = nullptr;
        return true;
    }
    return false;
}

usize ParseUnsignedConstant(const MStringView text, const SymbolTable& symbols)noexcept{
    if(text.empty())
        return 1u;

    i64 evaluated = 0;
    if(TryEvaluateTextConstant(text, symbols, evaluated))
        return evaluated > 0 ? static_cast<usize>(evaluated) : 1u;
    return 1u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

