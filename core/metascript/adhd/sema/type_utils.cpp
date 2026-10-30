// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "type_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_type_utils{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 IsValidConstTemplateArgGlobal(const Stmt* globalConst, const SymbolTable& symbols)noexcept{
    if(!globalConst || !globalConst->varType)
        return false;
    const MStringView baseName = symbols.resolveBaseTypeName(globalConst->varType->name);
    return baseName == "b8" || IsIntegerScalarType(baseName) || symbols.findEnum(baseName) != nullptr;
}

[[nodiscard]] MStringView TrimAsciiWhitespace(MStringView text)noexcept{
    while(!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' || text.front() == '\n'))
        text.remove_prefix(1u);
    while(!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' || text.back() == '\n'))
        text.remove_suffix(1u);
    return text;
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

[[nodiscard]] b8 IsValidScopedEnumTemplateArg(MStringView text, const SymbolTable& symbols, const EnumDecl** outEnum = nullptr, MStringView* outEntryName = nullptr)noexcept{
    text = TrimAsciiWhitespace(text);
    const usize firstSep = text.find("::");
    if(firstSep == MStringView::npos || firstSep == 0u)
        return false;
    const MStringView enumName = TrimAsciiWhitespace(text.substr(0u, firstSep));
    const EnumDecl* ed = symbols.findEnum(symbols.resolveBaseTypeName(enumName));
    if(!ed)
        return false;

    const MStringView rest = TrimAsciiWhitespace(text.substr(firstSep + 2u));
    MStringView entryName = rest;
    const usize secondSep = rest.find("::");
    if(secondSep != MStringView::npos){
        const MStringView mid = TrimAsciiWhitespace(rest.substr(0u, secondSep));
        if(mid != "Enum" && mid != "Flag")
            return false;
        entryName = TrimAsciiWhitespace(rest.substr(secondSep + 2u));
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

[[nodiscard]] b8 TryEvaluateTextConstant(MStringView text, const SymbolTable& symbols, i64& outVal, u32 depth = 0u, const EnumDecl* contextEnum = nullptr)noexcept;

[[nodiscard]] b8 TryEvaluateExprConstant(const Expr* expr, const SymbolTable& symbols, i64& outVal, const u32 depth = 0u, const EnumDecl* contextEnum = nullptr)noexcept{
    if(!expr || depth > 16u)
        return false;

    switch(expr->kind){
    case ExprKind::IntegerLiteral:
    case ExprKind::Identifier:
        return TryEvaluateTextConstant(expr->text, symbols, outVal, depth + 1u, contextEnum);
    case ExprKind::BoolLiteral:
        outVal = (expr->text == "true") ? 1 : 0;
        return true;
    case ExprKind::ScopeAccess: {
        if(expr->scopeChain.size() < 2u)
            return false;
        const EnumDecl* ed = symbols.findEnum(symbols.resolveBaseTypeName(expr->scopeChain[0]));
        if(!ed)
            return false;
        const MStringView targetEntry = expr->scopeChain.back();
        i64 nextSeq = 0;
        usize flagBitIndex = 0u;
        for(const EnumEntry& entry : ed->entries){
            i64 curVal = 0;
            if(entry.valueExpr){
                if(!TryEvaluateExprConstant(entry.valueExpr, symbols, curVal, depth + 1u, ed))
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
    case ExprKind::Unary: {
        if(expr->isPostfix)
            return false;
        i64 inner = 0;
        if(!TryEvaluateExprConstant(expr->lhs, symbols, inner, depth + 1u, contextEnum))
            return false;
        switch(expr->op){
        case TokenType::Plus: outVal = +inner; return true;
        case TokenType::Minus: outVal = -inner; return true;
        case TokenType::Tilde: outVal = ~inner; return true;
        case TokenType::Bang: outVal = (inner == 0) ? 1 : 0; return true;
        default: return false;
        }
    }
    case ExprKind::Binary: {
        i64 lhsVal = 0;
        i64 rhsVal = 0;
        if(!TryEvaluateExprConstant(expr->lhs, symbols, lhsVal, depth + 1u, contextEnum)
            || !TryEvaluateExprConstant(expr->rhs, symbols, rhsVal, depth + 1u, contextEnum))
        {
            return false;
        }
        switch(expr->op){
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
    case ExprKind::Cast:
        return TryEvaluateExprConstant(expr->lhs, symbols, outVal, depth + 1u, contextEnum);
    default:
        return false;
    }
}

[[nodiscard]] b8 TryEvaluateTextConstant(MStringView text, const SymbolTable& symbols, i64& outVal, const u32 depth, const EnumDecl* contextEnum)noexcept{
    if(depth > 16u)
        return false;
    text = TrimAsciiWhitespace(text);
    while(HasMatchingOuterParens(text))
        text = TrimAsciiWhitespace(text.substr(1u, text.size() - 2u));
    if(text.empty())
        return false;

    auto findTopLevelBinary = [&](const MStringView op1, const MStringView op2, usize& outPos, usize& outLen)noexcept -> b8{
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

            auto matchesAt = [&](const MStringView op)noexcept -> b8{
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
                    MStringView prefix = TrimAsciiWhitespace(text.substr(0u, i));
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

    usize opPos = 0u;
    usize opLen = 0u;
    if(findTopLevelBinary("|", "", opPos, opLen)){
        i64 l = 0, r = 0;
        if(!TryEvaluateTextConstant(text.substr(0u, opPos), symbols, l, depth + 1u, contextEnum)
            || !TryEvaluateTextConstant(text.substr(opPos + opLen), symbols, r, depth + 1u, contextEnum))
            return false;
        outVal = l | r;
        return true;
    }
    if(findTopLevelBinary("^", "", opPos, opLen)){
        i64 l = 0, r = 0;
        if(!TryEvaluateTextConstant(text.substr(0u, opPos), symbols, l, depth + 1u, contextEnum)
            || !TryEvaluateTextConstant(text.substr(opPos + opLen), symbols, r, depth + 1u, contextEnum))
            return false;
        outVal = l ^ r;
        return true;
    }
    if(findTopLevelBinary("&", "", opPos, opLen)){
        i64 l = 0, r = 0;
        if(!TryEvaluateTextConstant(text.substr(0u, opPos), symbols, l, depth + 1u, contextEnum)
            || !TryEvaluateTextConstant(text.substr(opPos + opLen), symbols, r, depth + 1u, contextEnum))
            return false;
        outVal = l & r;
        return true;
    }
    if(findTopLevelBinary("<<", ">>", opPos, opLen)){
        i64 l = 0, r = 0;
        if(!TryEvaluateTextConstant(text.substr(0u, opPos), symbols, l, depth + 1u, contextEnum)
            || !TryEvaluateTextConstant(text.substr(opPos + opLen), symbols, r, depth + 1u, contextEnum)
            || r < 0 || r >= 63)
            return false;
        outVal = (text.substr(opPos, opLen) == "<<")
            ? static_cast<i64>(static_cast<u64>(l) << static_cast<u64>(r))
            : (l >> r);
        return true;
    }
    if(findTopLevelBinary("+", "-", opPos, opLen)){
        i64 l = 0, r = 0;
        if(!TryEvaluateTextConstant(text.substr(0u, opPos), symbols, l, depth + 1u, contextEnum)
            || !TryEvaluateTextConstant(text.substr(opPos + opLen), symbols, r, depth + 1u, contextEnum))
            return false;
        outVal = (text[opPos] == '+') ? (l + r) : (l - r);
        return true;
    }
    if(findTopLevelBinary("*", "/", opPos, opLen) || findTopLevelBinary("%", "", opPos, opLen)){
        i64 l = 0, r = 0;
        if(!TryEvaluateTextConstant(text.substr(0u, opPos), symbols, l, depth + 1u, contextEnum)
            || !TryEvaluateTextConstant(text.substr(opPos + opLen), symbols, r, depth + 1u, contextEnum))
            return false;
        if(text[opPos] == '*'){
            outVal = l * r;
            return true;
        }
        if(r == 0)
            return false;
        outVal = (text[opPos] == '/') ? (l / r) : (l % r);
        return true;
    }

    if(text[0] == '+' || text[0] == '-' || text[0] == '~' || text[0] == '!'){
        i64 inner = 0;
        if(!TryEvaluateTextConstant(text.substr(1u), symbols, inner, depth + 1u, contextEnum))
            return false;
        switch(text[0]){
        case '+': outVal = +inner; return true;
        case '-': outVal = -inner; return true;
        case '~': outVal = ~inner; return true;
        case '!': outVal = (inner == 0) ? 1 : 0; return true;
        default: return false;
        }
    }

    if(text == "true"){
        outVal = 1;
        return true;
    }
    if(text == "false"){
        outVal = 0;
        return true;
    }

    if(text.size() >= 3u && text.front() == '\'' && text.back() == '\''){
        const MStringView inner = text.substr(1u, text.size() - 2u);
        if(inner.size() == 1u){
            outVal = static_cast<i64>(static_cast<u8>(inner[0]));
            return true;
        }
        if(inner.size() >= 2u && inner[0] == '\\'){
            switch(inner[1]){
            case 'n': outVal = static_cast<i64>('\n'); return true;
            case 'r': outVal = static_cast<i64>('\r'); return true;
            case 't': outVal = static_cast<i64>('\t'); return true;
            case 'v': outVal = static_cast<i64>('\v'); return true;
            case 'b': outVal = static_cast<i64>('\b'); return true;
            case 'f': outVal = static_cast<i64>('\f'); return true;
            case 'a': outVal = static_cast<i64>('\a'); return true;
            case '\\': outVal = static_cast<i64>('\\'); return true;
            case '\'': outVal = static_cast<i64>('\''); return true;
            case '"': outVal = static_cast<i64>('"'); return true;
            case '?': outVal = static_cast<i64>('?'); return true;
            case 'x':
            case 'X':
            case 'u':
            case 'U': {
                u64 hexVal = 0u;
                for(usize i = 2u; i < inner.size(); ++i){
                    const MChar c = inner[i];
                    if(c >= '0' && c <= '9')
                        hexVal = (hexVal << 4u) | static_cast<u64>(c - '0');
                    else if(c >= 'a' && c <= 'f')
                        hexVal = (hexVal << 4u) | static_cast<u64>(10 + (c - 'a'));
                    else if(c >= 'A' && c <= 'F')
                        hexVal = (hexVal << 4u) | static_cast<u64>(10 + (c - 'A'));
                }
                outVal = static_cast<i64>(hexVal);
                return true;
            }
            default:
                if(inner[1] >= '0' && inner[1] <= '7'){
                    u64 octVal = 0u;
                    for(usize i = 1u; i < inner.size() && inner[i] >= '0' && inner[i] <= '7'; ++i)
                        octVal = (octVal << 3u) | static_cast<u64>(inner[i] - '0');
                    outVal = static_cast<i64>(octVal);
                    return true;
                }
                outVal = static_cast<i64>(static_cast<u8>(inner[1]));
                return true;
            }
        }
        return false;
    }

    if(text[0] >= '0' && text[0] <= '9'){
        u64 val = 0u;
        if(text.size() > 2u && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')){
            for(usize i = 2u; i < text.size(); ++i){
                const MChar c = text[i];
                if(c == '\'')
                    continue;
                if(c >= '0' && c <= '9')
                    val = val * 16u + static_cast<u64>(c - '0');
                else if(c >= 'a' && c <= 'f')
                    val = val * 16u + static_cast<u64>(10 + (c - 'a'));
                else if(c >= 'A' && c <= 'F')
                    val = val * 16u + static_cast<u64>(10 + (c - 'A'));
                else
                    break;
            }
            outVal = static_cast<i64>(val);
            return true;
        }
        if(text.size() > 2u && text[0] == '0' && (text[1] == 'b' || text[1] == 'B')){
            for(usize i = 2u; i < text.size(); ++i){
                const MChar c = text[i];
                if(c == '\'')
                    continue;
                if(c == '0' || c == '1')
                    val = val * 2u + static_cast<u64>(c - '0');
                else
                    break;
            }
            outVal = static_cast<i64>(val);
            return true;
        }
        for(usize i = 0u; i < text.size(); ++i){
            const MChar c = text[i];
            if(c == '\'')
                continue;
            if(c >= '0' && c <= '9')
                val = val * 10u + static_cast<u64>(c - '0');
            else
                break;
        }
        outVal = static_cast<i64>(val);
        return true;
    }

    const EnumDecl* scopedEnum = nullptr;
    MStringView scopedEntryName;
    if(IsValidScopedEnumTemplateArg(text, symbols, &scopedEnum, &scopedEntryName)){
        i64 nextSeq = 0;
        usize flagBitIndex = 0u;
        for(const EnumEntry& entry : scopedEnum->entries){
            i64 curVal = 0;
            if(entry.valueExpr){
                if(!TryEvaluateExprConstant(entry.valueExpr, symbols, curVal, depth + 1u, scopedEnum))
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
            if(entry.name == scopedEntryName){
                outVal = curVal;
                return true;
            }
        }
        return false;
    }

    if(contextEnum){
        i64 nextSeq = 0;
        usize flagBitIndex = 0u;
        for(const EnumEntry& entry : contextEnum->entries){
            i64 curVal = 0;
            if(entry.valueExpr){
                if(!TryEvaluateExprConstant(entry.valueExpr, symbols, curVal, depth + 1u, contextEnum))
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
            if(entry.name == text){
                outVal = curVal;
                return true;
            }
        }
    }

    if(const Stmt* globalConst = symbols.findGlobalConstant(text)){
        if(!IsValidConstTemplateArgGlobal(globalConst, symbols))
            return false;
        const Expr* valExpr = globalConst->expr;
        if(!valExpr && globalConst->isCtorInit && globalConst->arguments.size() == 1u)
            valExpr = globalConst->arguments[0];
        if(valExpr)
            return TryEvaluateExprConstant(valExpr, symbols, outVal, depth + 1u, contextEnum);
    }

    return false;
}

[[nodiscard]] b8 IsConcreteConstTemplateArgText(const MStringView text, const SymbolTable& symbols)noexcept{
    if(text.empty())
        return false;
    i64 dummyVal = 0;
    if(TryEvaluateTextConstant(text, symbols, dummyVal))
        return true;
    if(IsValidScopedEnumTemplateArg(text, symbols))
        return true;
    MStringView ident = TrimAsciiWhitespace(text);
    if(!ident.empty() && (ident.front() == '+' || ident.front() == '-'))
        ident = TrimAsciiWhitespace(ident.substr(1u));
    return IsValidConstTemplateArgGlobal(symbols.findGlobalConstant(ident), symbols);
}

[[nodiscard]] b8 AreConstantTemplateArgsEquivalent(const MStringView a, const MStringView b, const SymbolTable& symbols)noexcept{
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



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 IsKnownConcreteTypeName(const MStringView name, const SymbolTable& symbols)noexcept{
    if(name == "void")
        return true;
#define NWB_ADHD_EXPORT_SCALAR_TYPE(adhdName, byteSize, byteAlign, isInteger, isFixedWidthInt, isSigned, isFloat, defaultLiteral) \
    if(name == adhdName) return true;
#define NWB_ADHD_EXPORT_OBJECT_TYPE(adhdName, cppExportName, byteSize, byteAlign, isArenaBacked, isSelfDefaulted, isSmallValue, isStringSwitch, isNonThreadSafeContainer, isNwbMapKey) \
    if(name == adhdName) return true;
#define NWB_ADHD_EXPORT_GENERIC_TYPE(adhdName, cppExportName, paramKind, byteSize, byteAlign, isArenaBacked, isSelfDefaulted, isSmallValue, isNonThreadSafeContainer, isNonRandomAccessContainer, isIndexableSequence, isReflectedLinear, isReflectedMap) \
    if(name == adhdName && MStringView(#paramKind) != "NotNullType") return true;
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_GENERIC_TYPE
#undef NWB_ADHD_EXPORT_OBJECT_TYPE
#undef NWB_ADHD_EXPORT_SCALAR_TYPE
    return SymbolTable::isSimdType(name)
        || SymbolTable::isUnalignedVectorType(name)
        || symbols.findEnum(name) != nullptr
        || symbols.findInterface(name) != nullptr
        || symbols.findClass(name) != nullptr
        || symbols.findTypeAlias(name) != nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 IsNonThreadSafeContainer(const MStringView typeName)noexcept{
#define NWB_ADHD_EXPORT_OBJECT_TYPE(adhdName, cppExportName, byteSize, byteAlign, isArenaBacked, isSelfDefaulted, isSmallValue, isStringSwitch, isNonThreadSafeContainer, isNwbMapKey) \
    if constexpr(isNonThreadSafeContainer){ \
        if(typeName == adhdName) return true; \
    }
#define NWB_ADHD_EXPORT_GENERIC_TYPE(adhdName, cppExportName, paramKind, byteSize, byteAlign, isArenaBacked, isSelfDefaulted, isSmallValue, isNonThreadSafeContainer, isNonRandomAccessContainer, isIndexableSequence, isReflectedLinear, isReflectedMap) \
    if constexpr(isNonThreadSafeContainer){ \
        if(typeName == adhdName) return true; \
    }
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_GENERIC_TYPE
#undef NWB_ADHD_EXPORT_OBJECT_TYPE
    return false;
}

[[nodiscard]] b8 CheckSwizzlePattern(const MStringView text, const u32 maxLanes, b8& outIsPattern, b8& outValidLanes)noexcept{
    outIsPattern = false;
    outValidLanes = true;
    if(text.empty() || text.size() > 4u)
        return false;

    b8 usesXYZW = false;
    b8 usesRGBA = false;
    u32 highestLane = 0u;

    for(usize i = 0u; i < text.size(); ++i){
        const MChar c = text[i];
        u32 lane = 0u;
        b8 matchedChar = false;
#define NWB_ADHD_EXPORT_SWIZZLE_COMPONENT(xyzwChar, rgbaChar, laneIndex) \
        if(c == xyzwChar){ \
            usesXYZW = true; \
            lane = laneIndex; \
            matchedChar = true; \
        } \
        else if(c == rgbaChar){ \
            usesRGBA = true; \
            lane = laneIndex; \
            matchedChar = true; \
        }
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_SWIZZLE_COMPONENT
        if(!matchedChar)
            return false;

        if(lane >= highestLane)
            highestLane = lane + 1u;
    }

    if(usesXYZW && usesRGBA){
        outIsPattern = true;
        outValidLanes = false;
        return true;
    }

    outIsPattern = true;
    outValidLanes = (highestLane <= maxLanes);
    return true;
}

[[nodiscard]] b8 IsSingleTypeParamBuiltinGeneric(const MStringView name)noexcept{
    enum class GenericParamKind : u8{ SingleType, TwoType, TypeAndCount, NotNullType };
#define NWB_ADHD_EXPORT_GENERIC_TYPE(adhdName, cppExportName, paramKind, byteSize, byteAlign, isArenaBacked, isSelfDefaulted, isSmallValue, isNonThreadSafeContainer, isNonRandomAccessContainer, isIndexableSequence, isReflectedLinear, isReflectedMap) \
    if constexpr(GenericParamKind::paramKind == GenericParamKind::SingleType){ \
        if(name == adhdName) return true; \
    }
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_GENERIC_TYPE
    return false;
}

[[nodiscard]] b8 IsTwoTypeParamBuiltinGeneric(const MStringView name)noexcept{
    enum class GenericParamKind : u8{ SingleType, TwoType, TypeAndCount, NotNullType };
#define NWB_ADHD_EXPORT_GENERIC_TYPE(adhdName, cppExportName, paramKind, byteSize, byteAlign, isArenaBacked, isSelfDefaulted, isSmallValue, isNonThreadSafeContainer, isNonRandomAccessContainer, isIndexableSequence, isReflectedLinear, isReflectedMap) \
    if constexpr(GenericParamKind::paramKind == GenericParamKind::TwoType){ \
        if(name == adhdName) return true; \
    }
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_GENERIC_TYPE
    return false;
}

[[nodiscard]] b8 IsTypeAndCountBuiltinGeneric(const MStringView name)noexcept{
    enum class GenericParamKind : u8{ SingleType, TwoType, TypeAndCount, NotNullType };
#define NWB_ADHD_EXPORT_GENERIC_TYPE(adhdName, cppExportName, paramKind, byteSize, byteAlign, isArenaBacked, isSelfDefaulted, isSmallValue, isNonThreadSafeContainer, isNonRandomAccessContainer, isIndexableSequence, isReflectedLinear, isReflectedMap) \
    if constexpr(GenericParamKind::paramKind == GenericParamKind::TypeAndCount){ \
        if(name == adhdName) return true; \
    }
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_GENERIC_TYPE
    return false;
}

[[nodiscard]] b8 IsVoidTypeRef(const TypeRef* ref)noexcept{
    if(!ref)
        return true;
    return ref->name == "void"
        && ref->refMode == ParamPassingMode::None
        && !ref->isAuto
        && !ref->isFuncType
        && ref->templateArgs.empty();
}

[[nodiscard]] b8 AreTypeRefsEquivalent(const TypeRef* a, const TypeRef* b, const SymbolTable& symbols)noexcept{
    return AreTypeRefsEquivalentWithSubst(a, b, nullptr, nullptr, symbols);
}

[[nodiscard]] b8 AreTypeRefsEquivalentWithSubst(
    const TypeRef* actual,
    const TypeRef* pattern,
    const MVector<TemplateParamDecl>* ifaceParams,
    const MVector<TypeArg>* ifaceArgs,
    const SymbolTable& symbols
)noexcept{
    actual = symbols.unwrapTypeAlias(actual);
    pattern = symbols.unwrapTypeAlias(pattern);
    if(actual == pattern)
        return true;
    if(!actual || !pattern)
        return IsVoidTypeRef(actual) && IsVoidTypeRef(pattern);

    if(ifaceParams && ifaceArgs && pattern->templateArgs.empty() && !pattern->isFuncType){
        const usize substCount = ifaceParams->size() < ifaceArgs->size() ? ifaceParams->size() : ifaceArgs->size();
        for(usize k = 0u; k < substCount; ++k){
            if(!(*ifaceParams)[k].constValueType && (*ifaceParams)[k].name == pattern->name && (*ifaceArgs)[k].type){
                const TypeRef* subst = symbols.unwrapTypeAlias((*ifaceArgs)[k].type);
                const ParamPassingMode::Enum expectedRefMode = (pattern->refMode != ParamPassingMode::None)
                    ? pattern->refMode
                    : subst->refMode;
                if(actual->refMode != expectedRefMode)
                    return false;
                if(actual->name != subst->name
                    || actual->isAuto != subst->isAuto
                    || actual->isFuncType != subst->isFuncType
                    || actual->templateArgs.size() != subst->templateArgs.size())
                {
                    return false;
                }
                if(!AreTemplateArgListsEqual(actual->templateArgs, subst->templateArgs, symbols))
                    return false;
                if(actual->isFuncType){
                    if(!AreTypeRefsEquivalent(actual->funcReturnType, subst->funcReturnType, symbols)
                        || actual->funcParams.size() != subst->funcParams.size())
                    {
                        return false;
                    }
                    for(usize fi = 0u; fi < actual->funcParams.size(); ++fi){
                        if(actual->funcParams[fi].mode != subst->funcParams[fi].mode
                            || !AreTypeRefsEquivalent(actual->funcParams[fi].type, subst->funcParams[fi].type, symbols))
                        {
                            return false;
                        }
                    }
                }
                return true;
            }
        }
    }

    if(actual->name != pattern->name
        || actual->refMode != pattern->refMode
        || actual->isAuto != pattern->isAuto
        || actual->isFuncType != pattern->isFuncType
        || actual->templateArgs.size() != pattern->templateArgs.size())
    {
        return false;
    }
    for(usize i = 0u; i < actual->templateArgs.size(); ++i){
        const TypeArg& ta = actual->templateArgs[i];
        const TypeArg& tb = pattern->templateArgs[i];
        if(!__hidden_adhd_type_utils::AreConstantTemplateArgsEquivalent(ta.constantText, tb.constantText, symbols)
            || !AreTypeRefsEquivalentWithSubst(ta.type, tb.type, ifaceParams, ifaceArgs, symbols))
        {
            return false;
        }
    }
    if(actual->isFuncType){
        if(!AreTypeRefsEquivalentWithSubst(actual->funcReturnType, pattern->funcReturnType, ifaceParams, ifaceArgs, symbols)
            || actual->funcParams.size() != pattern->funcParams.size())
        {
            return false;
        }
        for(usize i = 0u; i < actual->funcParams.size(); ++i){
            if(actual->funcParams[i].mode != pattern->funcParams[i].mode
                || !AreTypeRefsEquivalentWithSubst(actual->funcParams[i].type, pattern->funcParams[i].type, ifaceParams, ifaceArgs, symbols))
            {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] b8 AreTemplateArgListsEqual(const MVector<TypeArg>& a, const MVector<TypeArg>& b, const SymbolTable& symbols)noexcept{
    if(a.size() != b.size())
        return false;
    for(usize i = 0u; i < a.size(); ++i){
        if(a[i].type && b[i].type){
            if(!AreTypeRefsEquivalent(a[i].type, b[i].type, symbols))
                return false;
        }
        else{
            const MStringView lhsName = a[i].type ? a[i].type->name : a[i].constantText;
            const MStringView rhsName = b[i].type ? b[i].type->name : b[i].constantText;
            if(!__hidden_adhd_type_utils::AreConstantTemplateArgsEquivalent(lhsName, rhsName, symbols))
                return false;
        }
    }
    return true;
}

[[nodiscard]] b8 DoParamListsMatch(const MVector<ParamDecl>& a, const MVector<ParamDecl>& b, const SymbolTable& symbols)noexcept{
    if(a.size() != b.size())
        return false;
    for(usize i = 0u; i < a.size(); ++i){
        if(a[i].mode != b[i].mode || !AreTypeRefsEquivalent(a[i].type, b[i].type, symbols))
            return false;
    }
    return true;
}

[[nodiscard]] TypeRef* ExtractUnderlyingFuncType(TypeRef* type, const SymbolTable& symbols)noexcept{
    for(u32 depth = 0u; type && depth < 16u; ++depth){
        if(type->isFuncType)
            return type;
        if(const TypeAliasDecl* aliasDecl = symbols.findTypeAlias(type->name)){
            if(aliasDecl->aliasedType){
                type = aliasDecl->aliasedType;
                continue;
            }
        }
        if((IsSingleTypeParamBuiltinGeneric(type->name) || IsTypeAndCountBuiltinGeneric(type->name))
            && !type->templateArgs.empty() && type->templateArgs[0].type)
        {
            type = type->templateArgs[0].type;
            continue;
        }
        break;
    }
    return nullptr;
}

[[nodiscard]] b8 HasConstValueTemplateParam(const ClassDecl* ownerClass, const MethodDecl* method, const MStringView name)noexcept{
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

[[nodiscard]] b8 NormalizeConstantTemplateArg(
    TypeArg& arg,
    const ClassDecl* enclosingClass,
    const MethodDecl* enclosingMethod,
    const SymbolTable& symbols
)noexcept{
    if(!arg.constantText.empty()){
        const MStringView ctext = __hidden_adhd_type_utils::TrimAsciiWhitespace(arg.constantText);
        if(__hidden_adhd_type_utils::IsConcreteConstTemplateArgText(ctext, symbols))
            return true;
        MStringView stripped = ctext;
        if(!stripped.empty() && (stripped.front() == '+' || stripped.front() == '-'))
            stripped = __hidden_adhd_type_utils::TrimAsciiWhitespace(stripped.substr(1u));
        return HasConstValueTemplateParam(enclosingClass, enclosingMethod, stripped);
    }
    if(!arg.type || arg.type->refMode != ParamPassingMode::None || !arg.type->templateArgs.empty() || arg.type->isFuncType)
        return false;
    const MStringView name = arg.type->name;
    if(name == "true" || name == "false"
        || __hidden_adhd_type_utils::IsConcreteConstTemplateArgText(name, symbols)
        || HasConstValueTemplateParam(enclosingClass, enclosingMethod, name))
    {
        arg.constantText = name;
        arg.type = nullptr;
        return true;
    }
    return false;
}

[[nodiscard]] usize ParseUnsignedConstant(const MStringView text, const SymbolTable& symbols)noexcept{
    if(text.empty())
        return 1u;

    i64 evaluated = 0;
    if(__hidden_adhd_type_utils::TryEvaluateTextConstant(text, symbols, evaluated))
        return evaluated > 0 ? static_cast<usize>(evaluated) : 1u;
    return 1u;
}

[[nodiscard]] b8 IsConcreteTypeRef(const TypeRef& type, const SymbolTable& symbols)noexcept{
    if(type.isAuto)
        return false;
    if(type.isFuncType){
        if(type.funcReturnType && !IsConcreteTypeRef(*type.funcReturnType, symbols))
            return false;
        for(const FuncParamType& fp : type.funcParams){
            if(fp.type && fp.mode == ParamPassingMode::None && !IsConcreteTypeRef(*fp.type, symbols))
                return false;
        }
        return true;
    }
    if(type.name == "AssetRef")
        return type.templateArgs.size() == 1u && type.templateArgs[0].type != nullptr;
    if(!IsKnownConcreteTypeName(type.name, symbols))
        return false;
    for(const TypeArg& arg : type.templateArgs){
        if(arg.type){
            if(!__hidden_adhd_type_utils::IsConcreteConstTemplateArgText(arg.type->name, symbols) && !IsConcreteTypeRef(*arg.type, symbols))
                return false;
        }
        else if(!arg.constantText.empty()){
            if(!__hidden_adhd_type_utils::IsConcreteConstTemplateArgText(arg.constantText, symbols))
                return false;
        }
    }
    return true;
}

[[nodiscard]] b8 IsConcreteTemplateArgList(
    const MVector<TemplateParamDecl>& params,
    const MVector<TypeArg>& args,
    const SymbolTable& symbols
)noexcept{
    if(params.size() != args.size())
        return false;
    for(usize i = 0u; i < params.size(); ++i){
        if(params[i].constValueType){
            const MStringView ctext = args[i].type ? args[i].type->name : args[i].constantText;
            if(!__hidden_adhd_type_utils::IsConcreteConstTemplateArgText(ctext, symbols))
                return false;
        }
        else{
            if(!args[i].type || !IsConcreteTypeRef(*args[i].type, symbols))
                return false;
        }
    }
    return true;
}

[[nodiscard]] b8 TypeContainsArenaBackedContainer(
    const TypeRef& type,
    const SymbolTable& symbols,
    const u32 depth
)noexcept{
    if(depth >= 8u || type.refMode != ParamPassingMode::None)
        return false;
    const TypeRef* unwrapped = symbols.unwrapTypeAlias(&type);
    if(!unwrapped || unwrapped->refMode != ParamPassingMode::None)
        return false;
    if(SymbolTable::isArenaBackedContainer(unwrapped->name))
        return true;
    for(const TypeArg& targ : unwrapped->templateArgs){
        if(targ.type && TypeContainsArenaBackedContainer(*targ.type, symbols, depth + 1u))
            return true;
    }
    return false;
}

[[nodiscard]] i32 ScoreCallableParams(
    const SymbolTable& symbols,
    const MVector<ParamDecl>& params,
    const MVector<MStringView>* argTypeNames,
    const MVector<b8>* argIsMut
)noexcept{
    i32 score = 0;
    const usize count = params.size();
    for(usize i = 0u; i < count; ++i){
        if(argIsMut && i < argIsMut->size()){
            const b8 paramMut = params[i].mode == ParamPassingMode::Mut;
            if(paramMut == (*argIsMut)[i])
                score += 8;
        }
        if(argTypeNames && i < argTypeNames->size() && !(*argTypeNames)[i].empty() && params[i].type){
            const MStringView paramBase = symbols.resolveBaseTypeName(params[i].type->name);
            const MStringView argBase = symbols.resolveBaseTypeName((*argTypeNames)[i]);
            if(paramBase == argBase){
                score += 16;
            }
            else if((paramBase == "StringView" && (argBase == "String" || argBase == "CompactString"))
                || (paramBase == "Path" && argBase == "StringView"))
            {
                score += 8;
            }
        }
    }
    return score;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

