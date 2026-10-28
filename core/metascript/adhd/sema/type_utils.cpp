// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "type_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_type_utils{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool IsKnownConcreteTypeName(const MStringView name, const SymbolTable& symbols)noexcept{
    if(name == "bool" || name == "void"
        || IsIntegerScalarType(name) || IsFloatScalarType(name)
        || name == "Name" || name == "Path" || name == "String" || name == "StringView" || name == "CompactString"
        || IsSingleTypeParamBuiltinGeneric(name)
        || IsTwoTypeParamBuiltinGeneric(name)
        || IsTypeAndCountBuiltinGeneric(name))
    {
        return true;
    }
    return SymbolTable::isSimdType(name)
        || SymbolTable::isArenaBackedContainer(name)
        || symbols.findEnum(name) != nullptr
        || symbols.findInterface(name) != nullptr
        || symbols.findClass(name) != nullptr
        || symbols.findTypeAlias(name) != nullptr;
}

[[nodiscard]] bool IsConcreteConstTemplateArgText(const MStringView text, const SymbolTable& symbols)noexcept{
    if(text.empty())
        return false;
    const bool isNumericOrBoolConst = (text[0] >= '0' && text[0] <= '9') || text == "true" || text == "false";
    return isNumericOrBoolConst || symbols.findGlobalConstant(text) != nullptr;
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool IsNonThreadSafeContainer(const MStringView typeName)noexcept{
    return typeName == "Vector"
        || typeName == "FixedVector"
        || typeName == "ForwardList"
        || typeName == "List"
        || typeName == "Deque"
        || typeName == "Set"
        || typeName == "HashSet"
        || typeName == "Map"
        || typeName == "HashMap"
        || typeName == "Queue"
        || typeName == "String";
}

[[nodiscard]] bool CheckSwizzlePattern(const MStringView text, const u32 maxLanes, bool& outIsPattern, bool& outValidLanes)noexcept{
    outIsPattern = false;
    outValidLanes = true;
    if(text.empty() || text.size() > 4u)
        return false;

    bool usesXYZW = false;
    bool usesRGBA = false;
    u32 highestLane = 0u;

    for(usize i = 0u; i < text.size(); ++i){
        const MChar c = text[i];
        u32 lane = 0u;
        if(c == 'x' || c == 'y' || c == 'z' || c == 'w'){
            usesXYZW = true;
            lane = (c == 'x') ? 0u : (c == 'y') ? 1u : (c == 'z') ? 2u : 3u;
        }
        else if(c == 'r' || c == 'g' || c == 'b' || c == 'a'){
            usesRGBA = true;
            lane = (c == 'r') ? 0u : (c == 'g') ? 1u : (c == 'b') ? 2u : 3u;
        }
        else{
            return false;
        }

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

[[nodiscard]] bool IsSingleTypeParamBuiltinGeneric(const MStringView name)noexcept{
    return name == "Vector"
        || name == "ParallelVector"
        || name == "ForwardList"
        || name == "List"
        || name == "Deque"
        || name == "Set"
        || name == "HashSet"
        || name == "ParallelHashSet"
        || name == "Queue"
        || name == "ParallelQueue"
        || name == "Span"
        || name == "Optional"
        || name == "AssetRef"
        || name == "Atomic";
}

[[nodiscard]] bool IsTwoTypeParamBuiltinGeneric(const MStringView name)noexcept{
    return name == "Map"
        || name == "HashMap"
        || name == "ParallelHashMap"
        || name == "Expected";
}

[[nodiscard]] bool IsTypeAndCountBuiltinGeneric(const MStringView name)noexcept{
    return name == "Array"
        || name == "FixedVector"
        || name == "ParallelBlockQueue";
}

[[nodiscard]] bool IsVoidTypeRef(const TypeRef* ref)noexcept{
    if(!ref)
        return true;
    return ref->name == "void"
        && ref->refMode == ParamPassingMode::None
        && !ref->isAuto
        && !ref->isFuncType
        && ref->templateArgs.empty();
}

[[nodiscard]] bool AreTypeRefsEquivalent(const TypeRef* a, const TypeRef* b, const SymbolTable& symbols)noexcept{
    return AreTypeRefsEquivalentWithSubst(a, b, nullptr, nullptr, symbols);
}

[[nodiscard]] bool AreTypeRefsEquivalentWithSubst(
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
                return actual->name == subst->name
                    && actual->isAuto == subst->isAuto
                    && actual->isFuncType == subst->isFuncType
                    && actual->templateArgs.size() == subst->templateArgs.size()
                    && AreTypeRefsEquivalent(actual, subst, symbols);
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
        if(ta.constantText != tb.constantText || !AreTypeRefsEquivalentWithSubst(ta.type, tb.type, ifaceParams, ifaceArgs, symbols))
            return false;
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

[[nodiscard]] bool AreTemplateArgListsEqual(const MVector<TypeArg>& a, const MVector<TypeArg>& b, const SymbolTable& symbols)noexcept{
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
            if(lhsName != rhsName)
                return false;
        }
    }
    return true;
}

[[nodiscard]] bool DoParamListsMatch(const MVector<ParamDecl>& a, const MVector<ParamDecl>& b, const SymbolTable& symbols)noexcept{
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

[[nodiscard]] bool HasConstValueTemplateParam(const ClassDecl* ownerClass, const MethodDecl* method, const MStringView name)noexcept{
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

[[nodiscard]] bool NormalizeConstantTemplateArg(
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
        || symbols.findGlobalConstant(name) != nullptr
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

    if(const Stmt* globalConst = symbols.findGlobalConstant(text)){
        if(globalConst->expr && globalConst->expr->kind == ExprKind::IntegerLiteral)
            return ParseUnsignedConstant(globalConst->expr->text, symbols);
        if(globalConst->isCtorInit && globalConst->arguments.size() == 1u && globalConst->arguments[0] && globalConst->arguments[0]->kind == ExprKind::IntegerLiteral)
            return ParseUnsignedConstant(globalConst->arguments[0]->text, symbols);
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

[[nodiscard]] bool IsConcreteTypeRef(const TypeRef& type, const SymbolTable& symbols)noexcept{
    if(type.isAuto)
        return false;
    if(type.isFuncType){
        if(type.funcReturnType && !IsConcreteTypeRef(*type.funcReturnType, symbols))
            return false;
        for(const FuncParamType& fp : type.funcParams){
            if(fp.type && !IsConcreteTypeRef(*fp.type, symbols))
                return false;
        }
        return true;
    }
    if(!__hidden_adhd_type_utils::IsKnownConcreteTypeName(type.name, symbols))
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

[[nodiscard]] bool IsConcreteTemplateArgList(
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

[[nodiscard]] bool TypeContainsArenaBackedContainer(
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

