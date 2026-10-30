// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "symbol_table.h"
#include "type_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_symbol_table{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TypeRef* SubstituteAliasTypeRef(const TypeRef& origType, const Span<const TemplateParamDecl> templateParams, const Span<const TypeArg> templateArgs, MetaArena& arena){
    const usize count = templateParams.size() < templateArgs.size() ? templateParams.size() : templateArgs.size();
    for(usize p = 0u; p < count; ++p){
        if(!templateParams[p].constValueType && origType.name == templateParams[p].name && origType.templateArgs.empty() && templateArgs[p].type){
            if(origType.refMode != ParamPassingMode::None && templateArgs[p].type->refMode == ParamPassingMode::None){
                TypeRef* withMode = AllocateAstNode<TypeRef>(arena);
                *withMode = *templateArgs[p].type;
                withMode->refMode = origType.refMode;
                return withMode;
            }
            return templateArgs[p].type;
        }
    }

    if(origType.templateArgs.empty() && !origType.isFuncType)
        return const_cast<TypeRef*>(&origType);

    TypeRef* cloned = AllocateAstNode<TypeRef>(arena);
    cloned->name = origType.name;
    cloned->refMode = origType.refMode;
    cloned->funcReturnType = origType.funcReturnType ? SubstituteAliasTypeRef(*origType.funcReturnType, templateParams, templateArgs, arena) : nullptr;
    cloned->inferredCaptureBytes = origType.inferredCaptureBytes;
    cloned->span = origType.span;
    cloned->isFuncType = origType.isFuncType;
    cloned->isAuto = origType.isAuto;
    for(const FuncParamType& fp : origType.funcParams){
        FuncParamType subFp = fp;
        if(fp.type)
            subFp.type = SubstituteAliasTypeRef(*fp.type, templateParams, templateArgs, arena);
        cloned->funcParams.push_back(subFp);
    }
    for(const TypeArg& arg : origType.templateArgs){
        TypeArg subArg = arg;
        if(arg.type){
            b8 matchedConstParam = false;
            for(usize p = 0u; p < count; ++p){
                if(templateParams[p].constValueType && arg.type->name == templateParams[p].name){
                    subArg.type = nullptr;
                    subArg.constantText = templateArgs[p].constantOrTypeName();
                    matchedConstParam = true;
                    break;
                }
            }
            if(!matchedConstParam)
                subArg.type = SubstituteAliasTypeRef(*arg.type, templateParams, templateArgs, arena);
        }
        else if(!arg.constantText.empty()){
            for(usize p = 0u; p < count; ++p){
                if(templateParams[p].constValueType && arg.constantText == templateParams[p].name){
                    subArg.constantText = templateArgs[p].constantOrTypeName();
                    break;
                }
            }
        }
        cloned->templateArgs.push_back(subArg);
    }
    return cloned;
}

[[nodiscard]] MethodDecl* SubstituteInterfaceMethod(const SymbolTable& symbols, MethodDecl* method, const b8 hasSubst, const Span<const TemplateParamDecl> templateParams, const Span<const TypeArg> templateArgs, MetaArena& arena){
    if(!method || !hasSubst)
        return method;
    MethodDecl* cloned = AllocateAstNode<MethodDecl>(arena);
    cloned->access = method->access;
    cloned->name = method->name;
    cloned->returnType = method->returnType ? symbols.substituteTypeParams(*method->returnType, templateParams, templateArgs) : nullptr;
    for(const ParamDecl& p : method->params){
        ParamDecl subP = p;
        if(p.type)
            subP.type = symbols.substituteTypeParams(*p.type, templateParams, templateArgs);
        cloned->params.push_back(subP);
    }
    cloned->span = method->span;
    cloned->isOperator = method->isOperator;
    cloned->isConst = method->isConst;
    return cloned;
}

[[nodiscard]] i32 ScoreCandidateParams(const SymbolTable& symbols, const Span<const ParamDecl> params, const usize* preferredArgCount, const Span<const MStringView> argTypeNames, const Span<const u8> argIsMut)noexcept{
    i32 score = (!preferredArgCount || params.size() == *preferredArgCount) ? 64 : 0;
    score += ScoreCallableParams(symbols, params, argTypeNames, argIsMut);
    return score;
}

[[nodiscard]] b8 ConsiderScoredCallableCandidate(
    const SymbolTable& symbols,
    MethodDecl* candidate,
    const usize* preferredArgCount,
    const Span<const MStringView> argTypeNames,
    const Span<const u8> argIsMut,
    MethodDecl*& inOutBestMatch,
    i32& inOutBestScore
)noexcept{
    if(argTypeNames.empty() && argIsMut.empty()){
        if(!preferredArgCount || candidate->params.size() == *preferredArgCount){
            inOutBestMatch = candidate;
            return true;
        }
        if(!inOutBestMatch)
            inOutBestMatch = candidate;
        return false;
    }
    const i32 score = ScoreCandidateParams(symbols, candidate->params, preferredArgCount, argTypeNames, argIsMut);
    if(score > inOutBestScore){
        inOutBestScore = score;
        inOutBestMatch = candidate;
    }
    return false;
}

[[nodiscard]] const MethodDecl* FindScoredClassCallable(
    const SymbolTable& symbols,
    const ClassDecl& classDecl,
    const MStringView name,
    const b8 isOperator,
    const usize* preferredArgCount,
    const b8 preferConst,
    const b8 requirePublic,
    const Span<const MStringView> argTypeNames,
    const Span<const u8> argIsMut
)noexcept{
    const MethodDecl* bestMatch = nullptr;
    i32 bestScore = -1;
    for(const MethodDecl* m : classDecl.methods){
        if(!m || m->isConstructor || m->isDestructor || m->isOperator != isOperator || m->name != name)
            continue;
        i32 score = ScoreCandidateParams(symbols, m->params, preferredArgCount, argTypeNames, argIsMut);
        if(!requirePublic || m->access == AccessSpecifier::Public)
            score += 32;
        if(m->isConst == preferConst)
            score += 1;
        if(score > bestScore){
            bestScore = score;
            bestMatch = m;
        }
    }
    return bestMatch;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 SymbolTable::IsArenaBackedContainer(const MStringView typeName)noexcept{
    if(const BuiltinObjectTypeInfo* objInfo = FindBuiltinObjectType(typeName))
        return objInfo->isArenaBacked;
    if(const BuiltinGenericTypeInfo* info = FindBuiltinGenericType(typeName))
        return info->isArenaBacked;
    return false;
}

b8 SymbolTable::IsSimdVectorType(const MStringView typeName, u32& outLanes)noexcept{
    if(const BuiltinSimdVectorInfo* info = FindBuiltinSimdVectorType(typeName)){
        outLanes = info->lanes;
        return true;
    }
    return false;
}

b8 SymbolTable::IsUnalignedVectorType(const MStringView typeName, u32& outLanes)noexcept{
    if(const BuiltinUnalignedVectorInfo* info = FindBuiltinUnalignedVectorType(typeName)){
        outLanes = info->lanes;
        return true;
    }
    return false;
}

b8 SymbolTable::IsUnalignedVectorType(const MStringView typeName)noexcept{
    return FindBuiltinUnalignedVectorType(typeName) != nullptr;
}

b8 SymbolTable::IsSimdMatrixType(const MStringView typeName)noexcept{
    return FindBuiltinSimdMatrixType(typeName) != nullptr;
}

b8 SymbolTable::IsSimdType(const MStringView typeName)noexcept{
    u32 lanes = 0u;
    return IsSimdVectorType(typeName, lanes) || IsSimdMatrixType(typeName);
}

b8 SymbolTable::IsStringSwitchType(const MStringView typeName)noexcept{
    if(const BuiltinObjectTypeInfo* objInfo = FindBuiltinObjectType(typeName))
        return objInfo->isStringSwitch;
    return false;
}

const FieldDecl* SymbolTable::FindClassField(const ClassDecl& classDecl, const MStringView name)noexcept{
    for(const FieldDecl* field : classDecl.fields){
        if(field && field->name == name)
            return field;
    }
    return nullptr;
}

b8 SymbolTable::build(TranslationUnit& unit, MVector<ParseError>& errors){
    b8 ok = true;

    for(EnumDecl* decl : unit.enums){
        if(hasTypeSymbol(decl->name)){
            ReportAdhdDiagnostic(m_arena, errors, decl->span, "duplicate type symbol name");
            ok = false;
            continue;
        }
        m_enums.push_back(decl);
    }

    for(InterfaceDecl* decl : unit.interfaces){
        if(hasTypeSymbol(decl->name)){
            ReportAdhdDiagnostic(m_arena, errors, decl->span, "duplicate type symbol name");
            ok = false;
            continue;
        }
        m_interfaces.push_back(decl);
    }

    for(ClassDecl* decl : unit.classes){
        if(hasTypeSymbol(decl->name)){
            ReportAdhdDiagnostic(m_arena, errors, decl->span, "duplicate type symbol name");
            ok = false;
            continue;
        }
        m_classes.push_back(decl);
    }

    for(TypeAliasDecl* decl : unit.typeAliases){
        if(hasTypeSymbol(decl->name)){
            ReportAdhdDiagnostic(m_arena, errors, decl->span, "duplicate type symbol name");
            ok = false;
            continue;
        }
        m_typeAliases.push_back(decl);
    }

    for(MethodDecl* fn : unit.freeFunctions)
        m_freeFunctions.push_back(fn);

    for(Stmt* c : unit.globalConstants){
        if(findGlobalConstant(c->name)){
            ReportAdhdDiagnostic(m_arena, errors, c->span, "duplicate file-level 'const' name");
            ok = false;
            continue;
        }
        m_globalConstants.push_back(c);
    }

    return ok;
}

EnumDecl* SymbolTable::findEnum(const MStringView name)const noexcept{
    for(EnumDecl* decl : m_enums){
        if(decl->name == name)
            return decl;
    }
    return nullptr;
}

InterfaceDecl* SymbolTable::findInterface(const MStringView name)const noexcept{
    for(InterfaceDecl* decl : m_interfaces){
        if(decl->name == name)
            return decl;
    }
    return nullptr;
}

ClassDecl* SymbolTable::findClass(const MStringView name)const noexcept{
    for(ClassDecl* decl : m_classes){
        if(decl->name == name)
            return decl;
    }
    return nullptr;
}

TypeAliasDecl* SymbolTable::findTypeAlias(const MStringView name)const noexcept{
    for(TypeAliasDecl* decl : m_typeAliases){
        if(decl->name == name)
            return decl;
    }
    return nullptr;
}

MethodDecl* SymbolTable::findFreeFunction(const MStringView name, const usize* preferredArgCount, const Span<const MStringView> argTypeNames, const Span<const u8> argIsMut)const noexcept{
    MethodDecl* bestMatch = nullptr;
    i32 bestScore = -1;
    for(MethodDecl* fn : m_freeFunctions){
        if(!fn || fn->name != name)
            continue;
        if(__hidden_adhd_symbol_table::ConsiderScoredCallableCandidate(*this, fn, preferredArgCount, argTypeNames, argIsMut, bestMatch, bestScore))
            return bestMatch;
    }
    return bestMatch;
}

Stmt* SymbolTable::findGlobalConstant(const MStringView name)const noexcept{
    for(Stmt* c : m_globalConstants){
        if(c->name == name)
            return c;
    }
    return nullptr;
}

const MethodDecl* SymbolTable::findClassMethod(
    const ClassDecl& classDecl,
    const MStringView name,
    const usize* preferredArgCount,
    const b8 preferConst,
    const b8 requirePublic,
    const Span<const MStringView> argTypeNames,
    const Span<const u8> argIsMut
)const noexcept{
    return __hidden_adhd_symbol_table::FindScoredClassCallable(*this, classDecl, name, false, preferredArgCount, preferConst, requirePublic, argTypeNames, argIsMut);
}

const MethodDecl* SymbolTable::findClassOperator(
    const ClassDecl& classDecl,
    const MStringView opName,
    const usize* preferredArgCount,
    const b8 preferConst,
    const b8 requirePublic,
    const Span<const MStringView> argTypeNames,
    const Span<const u8> argIsMut
)const noexcept{
    return __hidden_adhd_symbol_table::FindScoredClassCallable(*this, classDecl, opName, true, preferredArgCount, preferConst, requirePublic, argTypeNames, argIsMut);
}

const MethodDecl* SymbolTable::findClassConstructor(const ClassDecl& classDecl, const usize argCount, const b8 requirePublic, const Span<const MStringView> argTypeNames, const Span<const u8> argIsMut)const noexcept{
    const MethodDecl* bestMatch = nullptr;
    i32 bestScore = -1;
    for(const MethodDecl* m : classDecl.methods){
        if(!m || !m->isConstructor || m->params.size() != argCount)
            continue;
        i32 score = (!requirePublic || m->access == AccessSpecifier::Public) ? 32 : 1;
        score += ScoreCallableParams(*this, m->params, argTypeNames, argIsMut);
        if(score > bestScore){
            bestScore = score;
            bestMatch = m;
        }
    }
    return bestMatch;
}

MethodDecl* SymbolTable::findInterfaceMethod(const TypeRef& ifaceType, const MStringView methodName, const usize* preferredArgCount, const u32 depth, const Span<const MStringView> argTypeNames, const Span<const u8> argIsMut)const noexcept{
    if(depth > 16u)
        return nullptr;
    const TypeRef* resolvedIface = unwrapTypeAlias(&ifaceType);
    if(!resolvedIface)
        return nullptr;
    const InterfaceDecl* iface = findInterface(resolvedIface->name);
    if(!iface)
        return nullptr;

    const b8 hasSubst = !iface->templateParams.empty() && !resolvedIface->templateArgs.empty();

    MethodDecl* bestMatch = nullptr;
    i32 bestScore = -1;
    for(MethodDecl* method : iface->methods){
        if(method && method->name == methodName){
            MethodDecl* subMethod = __hidden_adhd_symbol_table::SubstituteInterfaceMethod(*this, method, hasSubst, iface->templateParams, resolvedIface->templateArgs, m_arena);
            if(__hidden_adhd_symbol_table::ConsiderScoredCallableCandidate(*this, subMethod, preferredArgCount, argTypeNames, argIsMut, bestMatch, bestScore))
                return bestMatch;
        }
    }

    for(const TypeRef* baseType : iface->baseInterfaces){
        if(!baseType)
            continue;
        const TypeRef* subBase = substituteTypeParams(baseType, iface->templateParams, resolvedIface);
        if(!subBase)
            continue;
        MethodDecl* inherited = findInterfaceMethod(*subBase, methodName, preferredArgCount, depth + 1u, argTypeNames, argIsMut);
        if(inherited){
            if(__hidden_adhd_symbol_table::ConsiderScoredCallableCandidate(*this, inherited, preferredArgCount, argTypeNames, argIsMut, bestMatch, bestScore))
                return bestMatch;
        }
    }

    return bestMatch;
}

void SymbolTable::collectAllInterfaceMethods(const TypeRef& ifaceType, const u32 depth, MVector<MethodDecl*>& outMethods)const{
    if(depth > 16u)
        return;
    const TypeRef* resolvedIface = unwrapTypeAlias(&ifaceType);
    if(!resolvedIface)
        return;
    const InterfaceDecl* iface = findInterface(resolvedIface->name);
    if(!iface)
        return;

    const b8 hasSubst = !iface->templateParams.empty() && !resolvedIface->templateArgs.empty();
    for(const TypeRef* baseType : iface->baseInterfaces){
        if(!baseType)
            continue;
        const TypeRef* subBase = substituteTypeParams(baseType, iface->templateParams, resolvedIface);
        if(subBase)
            collectAllInterfaceMethods(*subBase, depth + 1u, outMethods);
    }

    for(MethodDecl* method : iface->methods){
        if(!method)
            continue;
        outMethods.push_back(__hidden_adhd_symbol_table::SubstituteInterfaceMethod(*this, method, hasSubst, iface->templateParams, resolvedIface->templateArgs, m_arena));
    }
}

TypeRef* SymbolTable::substituteTypeParams(const TypeRef& origType, const Span<const TemplateParamDecl> templateParams, const Span<const TypeArg> templateArgs)const{
    return __hidden_adhd_symbol_table::SubstituteAliasTypeRef(origType, templateParams, templateArgs, m_arena);
}

TypeRef* SymbolTable::substituteTypeParams(const TypeRef* origType, const Span<const TemplateParamDecl> templateParams, const TypeRef* instanceTypeRef)const{
    if(!origType || !instanceTypeRef || templateParams.empty() || instanceTypeRef->templateArgs.empty())
        return const_cast<TypeRef*>(origType);
    return substituteTypeParams(*origType, templateParams, instanceTypeRef->templateArgs);
}

const TypeRef* SymbolTable::unwrapTypeAlias(const TypeRef* type)const noexcept{
    if(!type)
        return nullptr;
    ParamPassingMode::Enum outerRefMode = type->refMode;
    const TypeRef* cur = type;
    for(u32 depth = 0u; cur && !cur->isFuncType && !cur->isAuto && depth < 16u; ++depth){
        const TypeAliasDecl* aliasDecl = findTypeAlias(cur->name);
        if(!aliasDecl || !aliasDecl->aliasedType)
            break;
        cur = substituteTypeParams(aliasDecl->aliasedType, aliasDecl->templateParams, cur);
        if(cur && outerRefMode == ParamPassingMode::None && cur->refMode != ParamPassingMode::None)
            outerRefMode = cur->refMode;
    }
    if(cur && outerRefMode != ParamPassingMode::None && cur->refMode != outerRefMode){
        TypeRef* withMode = AllocateAstNode<TypeRef>(m_arena);
        *withMode = *cur;
        withMode->refMode = outerRefMode;
        return withMode;
    }
    return cur;
}

ParamPassingMode::Enum SymbolTable::resolveRefMode(const TypeRef* type)const noexcept{
    if(!type)
        return ParamPassingMode::None;
    if(type->refMode != ParamPassingMode::None)
        return type->refMode;
    const TypeRef* unwrapped = unwrapTypeAlias(type);
    return unwrapped ? unwrapped->refMode : ParamPassingMode::None;
}

MStringView SymbolTable::resolveBaseTypeName(MStringView typeName)const noexcept{
    for(u32 depth = 0u; !typeName.empty() && depth < 16u; ++depth){
        const TypeAliasDecl* aliasDecl = findTypeAlias(typeName);
        if(!aliasDecl || !aliasDecl->aliasedType || aliasDecl->aliasedType->isFuncType)
            break;
        typeName = aliasDecl->aliasedType->name;
    }
    return typeName;
}

b8 SymbolTable::isSelfDefaultedType(const MStringView typeName)const noexcept{
    if(const TypeAliasDecl* aliasDecl = findTypeAlias(typeName)){
        if(aliasDecl->aliasedType && aliasDecl->aliasedType->isFuncType)
            return true;
    }
    const MStringView baseName = resolveBaseTypeName(typeName);
    if(const ClassDecl* cls = findClass(baseName)){
        b8 hasAnyCtor = false;
        for(const MethodDecl* m : cls->methods){
            if(m && m->isConstructor){
                hasAnyCtor = true;
                if(m->params.empty())
                    return true;
            }
        }
        return !hasAnyCtor;
    }
    if(IsUnalignedVectorType(baseName) || findEnum(baseName) != nullptr)
        return true;
    if(const BuiltinObjectTypeInfo* objInfo = FindBuiltinObjectType(baseName))
        return objInfo->isSelfDefaulted;
    if(const BuiltinGenericTypeInfo* info = FindBuiltinGenericType(baseName))
        return info->isSelfDefaulted;
    return false;
}

b8 SymbolTable::isSmallValueType(MStringView typeName)const noexcept{
    typeName = resolveBaseTypeName(typeName);
    if(IsScalarType(typeName) || findEnum(typeName) != nullptr)
        return true;
    if(const BuiltinObjectTypeInfo* objInfo = FindBuiltinObjectType(typeName))
        return objInfo->isSmallValue;
    if(const BuiltinGenericTypeInfo* info = FindBuiltinGenericType(typeName))
        return info->isSmallValue;
    return false;
}

b8 SymbolTable::hasTypeSymbol(const MStringView name)const noexcept{
    return findEnum(name) != nullptr || findInterface(name) != nullptr || findClass(name) != nullptr || findTypeAlias(name) != nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

