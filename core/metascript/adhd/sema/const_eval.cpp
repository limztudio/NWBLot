// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "const_eval.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_const_eval{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 IsValidConstTemplateArgGlobal(const Stmt* globalConst, const SymbolTable& symbols)noexcept{
    if(!globalConst || !globalConst->varType)
        return false;
    const MStringView baseName = symbols.resolveBaseTypeName(globalConst->varType->name);
    return baseName == "b8" || IsIntegerScalarType(baseName) || symbols.findEnum(baseName) != nullptr;
}

[[nodiscard]] b8 HasMatchingOuterParens(const MStringView text)noexcept{
    if(text.size() < 2u || text.front() != '(' || text.back() != ')')
        return false;
    u32 depth = 0u;
    for(usize i = 0u; i < text.size(); ++i){
        if(text[i] == '('){
            ++depth;
        }
        else if(text[i] == ')'){
            if(depth == 0u)
                return false;
            --depth;
            if(depth == 0u && i + 1u < text.size())
                return false;
        }
    }
    return depth == 0u;
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
        if(rhsVal == 0)
            return false;
        outVal = lhsVal / rhsVal;
        return true;
    case TokenType::Percent:
        if(rhsVal == 0)
            return false;
        outVal = lhsVal % rhsVal;
        return true;
    case TokenType::LessLess:
        if(rhsVal < 0 || rhsVal >= 63)
            return false;
        outVal = static_cast<i64>(static_cast<u64>(lhsVal) << static_cast<u64>(rhsVal));
        return true;
    case TokenType::GreaterGreater:
        if(rhsVal < 0 || rhsVal >= 63)
            return false;
        outVal = lhsVal >> rhsVal;
        return true;
    case TokenType::Pipe: outVal = lhsVal | rhsVal; return true;
    case TokenType::Caret: outVal = lhsVal ^ rhsVal; return true;
    case TokenType::Ampersand: outVal = lhsVal & rhsVal; return true;
    default: return false;
    }
}


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

    const MStringView rest = TrimView(text.substr(firstSep + 2u));
    MStringView entryName = rest;
    const usize secondSep = rest.find("::");
    if(secondSep != MStringView::npos){
        const MStringView mid = TrimView(rest.substr(0u, secondSep));
        if(mid != "Enum" && mid != "Flag")
            return false;
        entryName = TrimView(rest.substr(secondSep + 2u));
        if(entryName.find("::") != MStringView::npos)
            return false;
    }
    if(entryName.empty())
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
    i64 nextSeq = 0;
    usize flagBitIndex = 0u;
    for(const EnumEntry& entry : ed.entries){
        i64 curVal = 0;
        if(entry.valueExpr){
            if(!TryEvaluateExprConstant(entry.valueExpr, symbols, depth + 1u, &ed, curVal))
                return false;
            nextSeq = curVal + 1;
        }
        else if(entry.isFlag){
            curVal = static_cast<i64>(usize(1) << flagBitIndex++);
            nextSeq = curVal + 1;
        }
        else{
            curVal = nextSeq++;
        }
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
        outVal = (expr->text == "true") ? 1 : 0;
        return true;
    case ExprKind::ScopeAccess:{
        if(expr->scopeChain.size() < 2u)
            return false;
        const EnumDecl* ed = symbols.findEnum(symbols.resolveBaseTypeName(expr->scopeChain[0]));
        if(!ed)
            return false;
        return TryEvaluateEnumEntryConstant(*ed, expr->scopeChain.back(), symbols, depth, outVal);
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
    while(__hidden_adhd_const_eval::HasMatchingOuterParens(text))
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
    if(findTopLevelBinary("|", "", opPos, opLen))
        return evalBinaryOp(opPos, opLen, TokenType::Pipe);
    if(findTopLevelBinary("^", "", opPos, opLen))
        return evalBinaryOp(opPos, opLen, TokenType::Caret);
    if(findTopLevelBinary("&", "", opPos, opLen))
        return evalBinaryOp(opPos, opLen, TokenType::Ampersand);
    if(findTopLevelBinary("<<", ">>", opPos, opLen))
        return evalBinaryOp(opPos, opLen, (text.substr(opPos, opLen) == "<<") ? TokenType::LessLess : TokenType::GreaterGreater);
    if(findTopLevelBinary("+", "-", opPos, opLen))
        return evalBinaryOp(opPos, opLen, (text[opPos] == '+') ? TokenType::Plus : TokenType::Minus);
    if(findTopLevelBinary("*", "/", opPos, opLen) || findTopLevelBinary("%", "", opPos, opLen)){
        const TokenType::Enum op = (text[opPos] == '*') ? TokenType::Star : ((text[opPos] == '/') ? TokenType::Slash : TokenType::Percent);
        return evalBinaryOp(opPos, opLen, op);
    }

    if(text[0] == '+' || text[0] == '-' || text[0] == '~' || text[0] == '!'){
        i64 inner = 0;
        if(!TryEvaluateTextConstant(text.substr(1u), symbols, depth + 1u, contextEnum, inner))
            return false;
        const TokenType::Enum op = (text[0] == '+') ? TokenType::Plus : ((text[0] == '-') ? TokenType::Minus : ((text[0] == '~') ? TokenType::Tilde : TokenType::Bang));
        return __hidden_adhd_const_eval::TryApplyUnaryConstantOp(op, inner, outVal);
    }

    if(text == "true"){
        outVal = 1;
        return true;
    }
    if(text == "false"){
        outVal = 0;
        return true;
    }

    if((text.size() >= 3u && text.front() == '\'' && text.back() == '\'') || IsAsciiDigit(text[0])){
        u64 parsedVal = 0u;
        if(!TryParseUnsignedIntegerLiteral(text, parsedVal))
            return false;
        outVal = static_cast<i64>(parsedVal);
        return true;
    }

    const EnumDecl* scopedEnum = nullptr;
    MStringView scopedEntryName;
    if(IsValidScopedEnumTemplateArg(text, symbols, &scopedEnum, &scopedEntryName))
        return TryEvaluateEnumEntryConstant(*scopedEnum, scopedEntryName, symbols, depth, outVal);

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
    if(IsValidScopedEnumTemplateArg(text, symbols))
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
    const b8 aIsBool = (a == "true" || a == "false");
    const b8 bIsBool = (b == "true" || b == "false");
    if(aIsBool != bIsBool)
        return false;
    i64 valA = 0;
    i64 valB = 0;
    if(TryEvaluateTextConstant(a, symbols, valA) && TryEvaluateTextConstant(b, symbols, valB))
        return valA == valB;
    return false;
}

b8 HasConstValueTemplateParam(const ClassDecl* ownerClass, const MethodDecl* method, const MStringView name)noexcept{
    if(method){
        for(const TemplateParamDecl& tp : method->templateParams){
            if(tp.constValueType && tp.name == name)
                return true;
        }
    }
    if(ownerClass){
        for(const TemplateParamDecl& tp : ownerClass->templateParams){
            if(tp.constValueType && tp.name == name)
                return true;
        }
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
    if(name == "true" || name == "false" || IsConcreteConstTemplateArgText(name, symbols) || HasConstValueTemplateParam(enclosingClass, enclosingMethod, name)){
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

