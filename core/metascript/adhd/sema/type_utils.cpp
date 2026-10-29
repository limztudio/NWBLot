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

[[nodiscard]] b8 IsConcreteConstTemplateArgText(const MStringView text, const SymbolTable& symbols)noexcept{
    if(text.empty())
        return false;
    const b8 isCharConst = text.size() >= 3u && text.front() == '\'' && text.back() == '\'';
    const b8 isNegInt = text.size() >= 2u && text[0] == '-' && text[1] >= '0' && text[1] <= '9';
    const b8 isNumericOrBoolConst = (text[0] >= '0' && text[0] <= '9') || isNegInt || text == "true" || text == "false" || isCharConst;
    return isNumericOrBoolConst || IsValidConstTemplateArgGlobal(symbols.findGlobalConstant(text), symbols);
}

[[nodiscard]] b8 AreConstantTemplateArgsEquivalent(const MStringView a, const MStringView b, const SymbolTable& symbols)noexcept{
    if(a == b)
        return true;
    if(a.empty() || b.empty() || a == "true" || a == "false" || b == "true" || b == "false" || a[0] == '-' || b[0] == '-')
        return false;
    if(IsConcreteConstTemplateArgText(a, symbols) && IsConcreteConstTemplateArgText(b, symbols))
        return ParseUnsignedConstant(a, symbols) == ParseUnsignedConstant(b, symbols);
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
        || SymbolTable::isArenaBackedContainer(name)
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
        if(type->name == "Optional" && !type->templateArgs.empty() && type->templateArgs[0].type){
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
    if(!arg.constantText.empty())
        return true;
    if(!arg.type || arg.type->refMode != ParamPassingMode::None || !arg.type->templateArgs.empty() || arg.type->isFuncType)
        return false;
    const MStringView name = arg.type->name;
    if(name == "true" || name == "false"
        || __hidden_adhd_type_utils::IsValidConstTemplateArgGlobal(symbols.findGlobalConstant(name), symbols)
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

    MStringView curText = text;
    for(u32 depth = 0u; depth < 8u; ++depth){
        const Stmt* globalConst = symbols.findGlobalConstant(curText);
        if(!globalConst)
            break;
        const Expr* valExpr = globalConst->expr;
        if(!valExpr && globalConst->isCtorInit && globalConst->arguments.size() == 1u)
            valExpr = globalConst->arguments[0];
        if(valExpr && (valExpr->kind == ExprKind::IntegerLiteral || valExpr->kind == ExprKind::Identifier)){
            curText = valExpr->text;
            continue;
        }
        break;
    }
    if(curText != text)
        return ParseUnsignedConstant(curText, symbols);

    if(text.size() >= 3u && text.front() == '\'' && text.back() == '\''){
        const MStringView inner = text.substr(1u, text.size() - 2u);
        if(inner.size() == 1u)
            return static_cast<usize>(static_cast<u8>(inner[0]));
        if(inner.size() >= 2u && inner[0] == '\\'){
            switch(inner[1]){
            case 'n': return static_cast<usize>('\n');
            case 'r': return static_cast<usize>('\r');
            case 't': return static_cast<usize>('\t');
            case 'v': return static_cast<usize>('\v');
            case 'b': return static_cast<usize>('\b');
            case 'f': return static_cast<usize>('\f');
            case 'a': return static_cast<usize>('\a');
            case '\\': return static_cast<usize>('\\');
            case '\'': return static_cast<usize>('\'');
            case '"': return static_cast<usize>('"');
            case '?': return static_cast<usize>('?');
            case 'x':
            case 'X':
            case 'u':
            case 'U': {
                usize hexVal = 0u;
                for(usize i = 2u; i < inner.size(); ++i){
                    const MChar c = inner[i];
                    if(c >= '0' && c <= '9')
                        hexVal = (hexVal << 4u) | static_cast<usize>(c - '0');
                    else if(c >= 'a' && c <= 'f')
                        hexVal = (hexVal << 4u) | static_cast<usize>(10 + (c - 'a'));
                    else if(c >= 'A' && c <= 'F')
                        hexVal = (hexVal << 4u) | static_cast<usize>(10 + (c - 'A'));
                }
                return hexVal;
            }
            default:
                if(inner[1] >= '0' && inner[1] <= '7'){
                    usize octVal = 0u;
                    for(usize i = 1u; i < inner.size() && inner[i] >= '0' && inner[i] <= '7'; ++i)
                        octVal = (octVal << 3u) | static_cast<usize>(inner[i] - '0');
                    return octVal;
                }
                return static_cast<usize>(static_cast<u8>(inner[1]));
            }
        }
    }

    usize val = 0u;
    if(text.size() > 2u && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')){
        for(usize i = 2u; i < text.size(); ++i){
            const MChar c = text[i];
            if(c >= '0' && c <= '9')
                val = val * 16u + static_cast<usize>(c - '0');
            else if(c >= 'a' && c <= 'f')
                val = val * 16u + static_cast<usize>(10 + (c - 'a'));
            else if(c >= 'A' && c <= 'F')
                val = val * 16u + static_cast<usize>(10 + (c - 'A'));
            else
                break;
        }
        return val > 0u ? val : 1u;
    }
    if(text.size() > 2u && text[0] == '0' && (text[1] == 'b' || text[1] == 'B')){
        for(usize i = 2u; i < text.size(); ++i){
            const MChar c = text[i];
            if(c == '0' || c == '1')
                val = val * 2u + static_cast<usize>(c - '0');
            else
                break;
        }
        return val > 0u ? val : 1u;
    }
    for(usize i = 0u; i < text.size(); ++i){
        const MChar c = text[i];
        if(c >= '0' && c <= '9')
            val = val * 10u + static_cast<usize>(c - '0');
        else
            break;
    }
    return val > 0u ? val : 1u;
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

