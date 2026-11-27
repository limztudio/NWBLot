// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/metascript/adhd/ast/builtin_type_semantics.h>
#include <core/metascript/adhd/ast/operator_semantics.h>

#include "symbol_table.h"
#include "type_substitution.h"
#include "type_comparator.h"
#include "callable_overload_selector.h"
#include "type_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_symbol_table{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ObjPred, typename GenericPred>
[[nodiscard]] b8 MatchObjectOrGenericFlag(const MStringView name, ObjPred&& objPred, GenericPred&& genericPred)noexcept{
    if(const BuiltinObjectTypeInfo* objInfo = FindBuiltinObjectType(name))
        return objPred(*objInfo);
    if(const BuiltinGenericTypeInfo* genericInfo = FindBuiltinGenericType(name))
        return genericPred(*genericInfo);
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 SymbolTable::IsArenaBackedContainer(const MStringView typeName)noexcept{
    return __hidden_adhd_symbol_table::MatchObjectOrGenericFlag(
        typeName,
        [](const BuiltinObjectTypeInfo& obj)noexcept{ return obj.isArenaBacked; },
        [](const BuiltinGenericTypeInfo& info)noexcept{ return info.isArenaBacked; }
    );
}

b8 SymbolTable::IsSimdRegisterVectorType(const MStringView typeName)noexcept{
    if(const BuiltinSimdVectorInfo* info = FindBuiltinSimdVectorType(typeName))
        return info->cppExportName == info->regCppType;
    return false;
}

b8 SymbolTable::IsSimdRegisterMatrixType(const MStringView typeName)noexcept{
    if(const BuiltinSimdMatrixInfo* info = FindBuiltinSimdMatrixType(typeName))
        return info->cppExportName == info->regCppType;
    return false;
}

b8 SymbolTable::IsSimdRegisterType(const MStringView typeName)noexcept{
    return IsSimdRegisterVectorType(typeName) || IsSimdRegisterMatrixType(typeName);
}

b8 SymbolTable::IsSimdStorageVectorType(const MStringView typeName, u32& outLanes)noexcept{
    return !IsSimdRegisterVectorType(typeName) && IsSimdVectorType(typeName, outLanes);
}

b8 SymbolTable::IsSimdStorageMatrixType(const MStringView typeName)noexcept{
    return !IsSimdRegisterMatrixType(typeName) && IsSimdMatrixType(typeName);
}

b8 SymbolTable::IsSimdStorageType(const MStringView typeName)noexcept{
    return !IsSimdRegisterType(typeName) && IsSimdType(typeName);
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
    u32 lanes = 0;
    return IsUnalignedVectorType(typeName, lanes);
}

b8 SymbolTable::IsSimdMatrixType(const MStringView typeName)noexcept{
    return FindBuiltinSimdMatrixType(typeName) != nullptr;
}

b8 SymbolTable::IsSimdType(const MStringView typeName)noexcept{
    u32 lanes = 0u;
    return IsSimdVectorType(typeName, lanes) || IsSimdMatrixType(typeName);
}

b8 SymbolTable::IsSimdOrUnalignedStorageType(const MStringView typeName)noexcept{
    return IsSimdStorageType(typeName) || IsUnalignedVectorType(typeName);
}

b8 SymbolTable::IsSimdStorageOrUnalignedVectorType(const MStringView typeName, u32& outLanes)noexcept{
    return IsSimdStorageVectorType(typeName, outLanes) || IsUnalignedVectorType(typeName, outLanes);
}

b8 SymbolTable::IsStringSwitchType(const MStringView typeName)noexcept{
    if(const BuiltinObjectTypeInfo* objInfo = FindBuiltinObjectType(typeName))
        return objInfo->isStringSwitch;
    return false;
}

const FieldDecl* SymbolTable::FindClassField(const ClassDecl& classDecl, const MStringView name)noexcept{
    for(auto* owner = &classDecl; owner; owner = owner->concreteBase){
        for(const FieldDecl* field : owner->fields){
            if(field && field->name == name)
                return field;
        }
    }
    return nullptr;
}

b8 SymbolTable::canAccessMember(const ClassDecl& receiver, const ClassDecl* caller, const ClassDecl* declaringClass, const AccessSpecifier::Enum access)const noexcept{
    const ClassDecl& owner = declaringClass ? *declaringClass : receiver;
    if(access == AccessSpecifier::Public || canAccessPrivateMembers(owner, caller))
        return true;
    if(access == AccessSpecifier::Protected && caller){
        for(auto* object = &receiver; object; object = object->concreteBase){
            if(object == caller){
                for(auto* base = caller->concreteBase; base; base = base->concreteBase){
                    if(base == &owner)
                        return true;
                }
                break;
            }
        }
    }
    return false;
}

b8 SymbolTable::canAccessPrivateMembers(const ClassDecl& target, const ClassDecl* const caller)const noexcept{
    if(caller == &target)
        return true;
    if(!caller || target.friendClasses.empty() || !caller->templateParams.empty())
        return false;
    const MStringView targetNamespace = target.isImported ? target.cppNamespace : m_unit.cppNamespace;
    const MStringView callerNamespace = caller->isImported ? caller->cppNamespace : m_unit.cppNamespace;
    return targetNamespace == callerNamespace && ContainsStringView(target.friendClasses, caller->name);
}

b8 SymbolTable::HasCustomAssignOperator(const ClassDecl& classDecl)noexcept{
    for(const MethodDecl* m : classDecl.methods){
        if(m && m->isOperator && OperatorSemantics::MapOperatorSymbolToTokenType(m->name) == TokenType::Equal)
            return true;
    }
    return false;
}

b8 SymbolTable::build(TranslationUnit& unit, MVector<ParseError>& errors){
    FOUNDATION_ASSERT(&unit == &m_unit);
    b8 ok = true;

    m_typeMap.clear();
    m_globalConstantMap.clear();
    m_freeFunctions.clear();
    m_freeFunctionHead.clear();
    m_freeFunctionNext.clear();

    const usize totalTypes = unit.enums.size() + unit.interfaces.size() + unit.classes.size() + unit.typeAliases.size();
    m_typeMap.reserve(totalTypes);

    const auto registerTypeSymbols = [&](const auto& decls, const TypeSymbolKind::Enum kind){
        for(auto* decl : decls){
            if(!m_typeMap.emplace(decl->name, TypeSymbolEntry{ kind, decl, decl->cppNamespace }).second){
                ReportAdhdDiagnostic(m_arena, errors, decl->span, "duplicate type symbol name");
                ok = false;
            }
        }
    };

    registerTypeSymbols(unit.enums, TypeSymbolKind::EnumDeclKind);
    registerTypeSymbols(unit.interfaces, TypeSymbolKind::InterfaceDeclKind);
    registerTypeSymbols(unit.classes, TypeSymbolKind::ClassDeclKind);
    registerTypeSymbols(unit.typeAliases, TypeSymbolKind::TypeAliasDeclKind);

    for(ClassDecl* cls : unit.classes){
        cls->concreteBase = nullptr;
        for(FieldDecl* field : cls->fields)
            field->declaringClass = cls;
        for(MethodDecl* method : cls->methods)
            method->declaringClass = cls;
        for(const TypeRef* baseRef : cls->implementedInterfaces){
            const auto* base = baseRef ? findClass(resolveBaseTypeName(baseRef->name)) : nullptr;
            if(!base || !base->isExtensible)
                continue;
            if(cls->concreteBase){
                ReportAdhdDiagnostic(m_arena, errors, cls->span, "only one authored concrete base is supported");
                ok = false;
                break;
            }
            cls->concreteBase = base;
        }
    }
    for(ClassDecl* cls : unit.classes){
        const ClassDecl* current = cls->concreteBase;
        for(usize depth = 0u; current; ++depth, current = current->concreteBase){
            if(current == cls || depth >= unit.classes.size()){
                ReportAdhdDiagnostic(m_arena, errors, cls->span, "cyclic concrete class inheritance is prohibited");
                cls->concreteBase = nullptr;
                ok = false;
                break;
            }
        }
    }

    m_freeFunctions.reserve(unit.freeFunctions.size());
    m_freeFunctionNext.reserve(unit.freeFunctions.size());
    HashMap<MStringView, usize, MetaArena, MStringViewHash> freeFunctionTail(m_arena);
    freeFunctionTail.reserve(unit.freeFunctions.size());
    m_freeFunctionHead.reserve(unit.freeFunctions.size());
    for(MethodDecl* fn : unit.freeFunctions){
        const usize idx = m_freeFunctions.size();
        m_freeFunctions.push_back(fn);
        m_freeFunctionNext.push_back(s_InvalidFreeFunctionIndex);
        if(!fn)
            continue;
        auto [tailIt, inserted] = freeFunctionTail.emplace(fn->name, idx);
        if(inserted){
            m_freeFunctionHead.emplace(fn->name, idx);
        }
        else{
            m_freeFunctionNext[tailIt->second] = idx;
            tailIt.value() = idx;
        }
    }

    m_globalConstantMap.reserve(unit.globalConstants.size());
    for(Stmt* c : unit.globalConstants){
        if(!m_globalConstantMap.emplace(c->name, c).second){
            ReportAdhdDiagnostic(m_arena, errors, c->span, "duplicate file-level 'const' name");
            ok = false;
        }
    }

    return ok;
}

EnumDecl* SymbolTable::findEnum(const MStringView name)const noexcept{
    const auto it = m_typeMap.find(name);
    return (it != m_typeMap.end() && it->second.kind == TypeSymbolKind::EnumDeclKind) ? static_cast<EnumDecl*>(it->second.decl) : nullptr;
}

InterfaceDecl* SymbolTable::findInterface(const MStringView name)const noexcept{
    const auto it = m_typeMap.find(name);
    return (it != m_typeMap.end() && it->second.kind == TypeSymbolKind::InterfaceDeclKind) ? static_cast<InterfaceDecl*>(it->second.decl) : nullptr;
}

ClassDecl* SymbolTable::findClass(const MStringView name)const noexcept{
    const auto it = m_typeMap.find(name);
    return (it != m_typeMap.end() && it->second.kind == TypeSymbolKind::ClassDeclKind) ? static_cast<ClassDecl*>(it->second.decl) : nullptr;
}

TypeAliasDecl* SymbolTable::findTypeAlias(const MStringView name)const noexcept{
    const auto it = m_typeMap.find(name);
    return (it != m_typeMap.end() && it->second.kind == TypeSymbolKind::TypeAliasDeclKind) ? static_cast<TypeAliasDecl*>(it->second.decl) : nullptr;
}

MethodDecl* SymbolTable::findFreeFunction(const MStringView name, const usize* preferredArgCount, const Span<const MStringView> argTypeNames, const Span<const u8> argIsMut)const noexcept{
    const auto it = m_freeFunctionHead.find(name);
    if(it == m_freeFunctionHead.end())
        return nullptr;
    MethodDecl* bestMatch = nullptr;
    i32 bestScore = -1;
    for(usize idx = it->second; idx != s_InvalidFreeFunctionIndex; idx = m_freeFunctionNext[idx]){
        MethodDecl* const fn = m_freeFunctions[idx];
        if(CallableOverloadSelector::ConsiderScoredCallableCandidate(*this, preferredArgCount, argTypeNames, argIsMut, *fn, bestMatch, bestScore))
            return bestMatch;
    }
    return bestMatch;
}

const MethodDecl* SymbolTable::findUniqueFreeFunction(const MStringView name, b8& outAmbiguous)const noexcept{
    outAmbiguous = false;
    const auto it = m_freeFunctionHead.find(name);
    if(it == m_freeFunctionHead.end())
        return nullptr;
    const MethodDecl* function = nullptr;
    const TypeComparator comparator(*this);
    for(usize idx = it->second; idx != s_InvalidFreeFunctionIndex; idx = m_freeFunctionNext[idx]){
        const MethodDecl* const candidate = m_freeFunctions[idx];
        if(
            !candidate->templateParams.empty()
            || (function && (
                function->cppNamespace != candidate->cppNamespace
                || !comparator.doParamListsMatch(function->params, candidate->params)
                || !comparator.areEquivalent(function->returnType, candidate->returnType)
            ))
        ){
            outAmbiguous = true;
            return nullptr;
        }
        if(!function || candidate->hasBody)
            function = candidate;
    }
    return function;
}

Stmt* SymbolTable::findGlobalConstant(const MStringView name)const noexcept{
    const auto it = m_globalConstantMap.find(name);
    return (it != m_globalConstantMap.end()) ? it->second : nullptr;
}

b8 SymbolTable::hasMultipleFreeFunctions(const MStringView name)const noexcept{
    const auto it = m_freeFunctionHead.find(name);
    return it != m_freeFunctionHead.end() && m_freeFunctionNext[it->second] != s_InvalidFreeFunctionIndex;
}

b8 SymbolTable::hasMultipleClassMethods(const ClassDecl& classDecl, const MStringView name, const b8 staticOnly)const noexcept{
    for(auto* owner = &classDecl; owner; owner = owner->concreteBase){
        u32 matchCount = 0u;
        b8 anyWithName = false;
        for(const MethodDecl* m : owner->methods){
            if(!m || m->isConstructor || m->isDestructor || m->isOperator || m->name != name)
                continue;
            anyWithName = true;
            if(staticOnly && !m->isStatic)
                continue;
            ++matchCount;
            if(matchCount > 1u)
                return true;
        }
        if(matchCount > 0u || anyWithName)
            return false;
    }
    return false;
}

void SymbolTable::countInterfaceMethodsUpToTwo(const TypeRef& ifaceType, const MStringView methodName, const u32 depth, u32& inOutCount)const noexcept{
    if(depth > 16u || inOutCount >= 2u)
        return;
    const TypeRef* resolvedIface = unwrapTypeAlias(&ifaceType);
    if(!resolvedIface)
        return;
    const InterfaceDecl* iface = findInterface(resolvedIface->name);
    if(!iface)
        return;
    for(const MethodDecl* method : iface->methods){
        if(method && method->name == methodName){
            ++inOutCount;
            if(inOutCount >= 2u)
                return;
        }
    }
    for(const TypeRef* baseType : iface->baseInterfaces){
        if(!baseType)
            continue;
        const TypeRef* subBase = substituteTypeParams(baseType, iface->templateParams, resolvedIface);
        if(!subBase)
            continue;
        countInterfaceMethodsUpToTwo(*subBase, methodName, depth + 1u, inOutCount);
        if(inOutCount >= 2u)
            return;
    }
}

b8 SymbolTable::hasMultipleInterfaceMethods(const TypeRef& ifaceType, const MStringView methodName, const u32 depth)const noexcept{
    u32 count = 0u;
    countInterfaceMethodsUpToTwo(ifaceType, methodName, depth, count);
    return count >= 2u;
}

const MethodDecl* SymbolTable::findClassMethod(
    const ClassDecl& classDecl,
    const MStringView name,
    const usize* preferredArgCount,
    const b8 preferConst,
    const b8 requirePublic,
    const Span<const MStringView> argTypeNames,
    const Span<const u8> argIsMut,
    const b8 staticOnly
)const noexcept{
    for(auto* owner = &classDecl; owner; owner = owner->concreteBase){
        if(const MethodDecl* method = CallableOverloadSelector::FindScoredClassCallable(*this, *owner, name, false, preferredArgCount, preferConst, requirePublic, argTypeNames, argIsMut, staticOnly))
            return method;
        for(const MethodDecl* local : owner->methods){
            if(local->name == name)
                return nullptr;
        }
    }
    return nullptr;
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
    return CallableOverloadSelector::FindScoredClassCallable(*this, classDecl, opName, true, preferredArgCount, preferConst, requirePublic, argTypeNames, argIsMut);
}

const MethodDecl* SymbolTable::findClassConstructor(const ClassDecl& classDecl, const usize argCount, const b8 requirePublic, const Span<const MStringView> argTypeNames, const Span<const u8> argIsMut)const noexcept{
    return CallableOverloadSelector::FindClassConstructor(*this, classDecl, argCount, requirePublic, argTypeNames, argIsMut);
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
            MethodDecl* subMethod = TypeSubstitution::SubstituteInterfaceMethod(*this, method, hasSubst, iface->templateParams, resolvedIface->templateArgs, m_arena);
            if(CallableOverloadSelector::ConsiderScoredCallableCandidate(*this, preferredArgCount, argTypeNames, argIsMut, *subMethod, bestMatch, bestScore))
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
            if(CallableOverloadSelector::ConsiderScoredCallableCandidate(*this, preferredArgCount, argTypeNames, argIsMut, *inherited, bestMatch, bestScore))
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
        outMethods.push_back(TypeSubstitution::SubstituteInterfaceMethod(*this, method, hasSubst, iface->templateParams, resolvedIface->templateArgs, m_arena));
    }
}

TypeRef* SymbolTable::substituteTypeParams(const TypeRef& origType, const Span<const TemplateParamDecl> templateParams, const Span<const TypeArg> templateArgs)const{
    return TypeSubstitution::SubstituteAliasType(origType, templateParams, templateArgs, m_arena, m_unit.substitutedConstantTexts);
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
    if(cur && outerRefMode != ParamPassingMode::None && cur->refMode != outerRefMode)
        return CloneTypeRefWithMode(m_arena, *cur, outerRefMode);
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

b8 SymbolTable::isSelfDefaultedType(const MStringView typeName, const ClassDecl* callerClass)const noexcept{
    if(const TypeAliasDecl* aliasDecl = findTypeAlias(typeName)){
        if(aliasDecl->aliasedType && aliasDecl->aliasedType->isFuncType)
            return false;
    }
    const MStringView baseName = resolveBaseTypeName(typeName);
    if(const ClassDecl* cls = findClass(baseName)){
        if(cls->isExtern && cls->isAbstract)
            return false;
        if(cls->isExtern && cls->isSelfDefaulted)
            return true;
        b8 hasAnyCtor = false;
        for(const MethodDecl* m : cls->methods){
            if(m && m->isConstructor){
                hasAnyCtor = true;
                const b8 allowsZeroArgs = m->params.empty() || (m->hasDefaultArgs && m->minArgCount == 0u);
                if(allowsZeroArgs && (callerClass == cls || m->access == AccessSpecifier::Public))
                    return true;
            }
        }
        return !hasAnyCtor;
    }
    if(findInterface(baseName))
        return false;
    if(IsUnalignedVectorType(baseName) || findEnum(baseName) != nullptr)
        return true;
    if(!hasTypeSymbol(baseName)){
        if(FindBuiltinEnum(baseName) != nullptr)
            return true;
        return __hidden_adhd_symbol_table::MatchObjectOrGenericFlag(
            baseName,
            [](const BuiltinObjectTypeInfo& obj)noexcept{ return obj.isSelfDefaulted; },
            [](const BuiltinGenericTypeInfo& info)noexcept{ return info.isSelfDefaulted; }
        );
    }
    return false;
}

b8 SymbolTable::isSmallValueType(MStringView typeName)const noexcept{
    typeName = resolveBaseTypeName(typeName);
    if(BuiltinTypeSemantics::IsScalarType(typeName) || IsSimdRegisterVectorType(typeName) || findEnum(typeName) != nullptr)
        return true;
    if(const ClassDecl* cls = findClass(typeName); cls && cls->isExtern)
        return cls->isSmallValue;
    if(!hasTypeSymbol(typeName)){
        if(FindBuiltinEnum(typeName) != nullptr)
            return true;
        return __hidden_adhd_symbol_table::MatchObjectOrGenericFlag(
            typeName,
            [](const BuiltinObjectTypeInfo& obj)noexcept{ return obj.isSmallValue; },
            [](const BuiltinGenericTypeInfo& info)noexcept{ return info.isSmallValue; }
        );
    }
    return false;
}

MStringView SymbolTable::findTypeNamespace(const MStringView name)const noexcept{
    const auto it = m_typeMap.find(name);
    return (it != m_typeMap.end()) ? it->second.cppNamespace : MStringView{};
}

b8 SymbolTable::hasTypeSymbol(const MStringView name)const noexcept{
    return m_typeMap.find(name) != m_typeMap.end();
}

b8 SymbolTable::isEnumType(const MStringView name)const noexcept{
    return findEnum(name) != nullptr || (!hasTypeSymbol(name) && FindBuiltinEnum(name) != nullptr);
}

b8 SymbolTable::isInterfaceOrAbstractType(const MStringView name)const noexcept{
    return findInterface(name) != nullptr || isAbstractBuiltinType(name);
}

b8 SymbolTable::isAbstractBuiltinType(const MStringView name)const noexcept{
    const MStringView baseName = resolveBaseTypeName(name);
    if(const ClassDecl* cls = findClass(baseName); cls && cls->isAbstract)
        return true;
    if(hasTypeSymbol(baseName))
        return false;
    if(IsNonCopyableBaseInterfaceName(baseName))
        return true;
    const BuiltinObjectTypeInfo* objInfo = FindBuiltinObjectType(baseName);
    return objInfo && objInfo->isAbstract;
}

b8 SymbolTable::isPolymorphicBaseBuiltinType(const MStringView name)const noexcept{
    const MStringView baseName = resolveBaseTypeName(name);
    if(const ClassDecl* cls = findClass(baseName); cls && cls->isExtern && cls->isPolymorphicBase)
        return true;
    if(hasTypeSymbol(baseName))
        return false;
    const BuiltinObjectTypeInfo* objInfo = FindBuiltinObjectType(baseName);
    return objInfo && objInfo->isPolymorphicBase;
}

const ClassDecl* SymbolTable::findPolymorphicSdkInterface(const MStringView baseName)const noexcept{
    if(const ClassDecl* sdk = findClass(baseName); sdk && sdk->isExtern && sdk->isPolymorphicBase)
        return sdk;
    return FindBuiltinPolymorphicInterfaceDecl(baseName);
}

b8 SymbolTable::hasPolymorphicInterface(const ClassDecl& classDecl)const noexcept{
    for(const TypeRef* ifaceRef : classDecl.implementedInterfaces){
        if(!ifaceRef)
            continue;
        const MStringView baseName = resolveBaseTypeName(ifaceRef->name);
        if(!IsNonCopyableBaseInterfaceName(baseName))
            return true;
    }
    return false;
}

b8 SymbolTable::hasPolymorphicBase(const ClassDecl& classDecl)const noexcept{
    return hasPolymorphicInterface(classDecl) || !classDecl.assetTypeName.empty() || !classDecl.systemTypeName.empty();
}

const BuiltinReceiverMethodInfo* SymbolTable::findInheritedBuiltinReceiverMethod(const ClassDecl* const cls, const InterfaceDecl* const iface, const MStringView methodName)const noexcept{
    if(cls){
        const ClassRoleContractInfo* roleContract = nullptr;
        if(!cls->assetTypeName.empty() || !cls->subsystemTypeName.empty())
            roleContract = ContractSemantics::FindClassRoleContract(ClassRoleKind::Asset);
        else if(!cls->systemTypeName.empty())
            roleContract = ContractSemantics::FindClassRoleContract(ClassRoleKind::System);
        if(roleContract && !roleContract->implicitInterfaceAdhdName.empty()){
            if(const BuiltinReceiverMethodInfo* info = FindBuiltinReceiverMethod(roleContract->implicitInterfaceAdhdName, methodName))
                return info;
        }
        for(const TypeRef* ifaceRef : cls->implementedInterfaces){
            if(!ifaceRef)
                continue;
            const MStringView ifaceName = resolveBaseTypeName(ifaceRef->name);
            if(!findInterface(ifaceName)){
                if(const BuiltinReceiverMethodInfo* info = FindBuiltinReceiverMethod(ifaceName, methodName))
                    return info;
            }
        }
        return nullptr;
    }
    if(iface && !iface->assetTypeName.empty()){
        if(const ClassRoleContractInfo* assetContract = ContractSemantics::FindClassRoleContract(ClassRoleKind::Asset); assetContract && !assetContract->implicitInterfaceAdhdName.empty())
            return FindBuiltinReceiverMethod(assetContract->implicitInterfaceAdhdName, methodName);
    }
    return nullptr;
}

MStringView SymbolTable::resolveRoleScopeMemberType(const MStringView scopeName, const MStringView memberName, const b8 allowStaticFields)const noexcept{
    const MStringView baseName = resolveBaseTypeName(scopeName);
    const auto resolveForRole = [memberName, allowStaticFields](const ClassRoleKind::Enum role)noexcept -> MStringView{
        if(ContractSemantics::IsClassRoleTypeNameMethod(role, memberName))
            return BuiltinTypeSemantics::NameTypeName();
        if(allowStaticFields){
            if(ContractSemantics::IsClassRoleTypeNameStaticField(role, memberName))
                return BuiltinTypeSemantics::NameTypeName();
            if(ContractSemantics::IsClassRoleTypeTextStaticField(role, memberName))
                return BuiltinTypeSemantics::StringViewTypeName();
        }
        return {};
    };
    if(const ClassDecl* scopedCls = findClass(baseName)){
        if(!scopedCls->assetTypeName.empty()){
            if(const MStringView t = resolveForRole(ClassRoleKind::Asset); !t.empty())
                return t;
        }
        if(!scopedCls->subsystemTypeName.empty()){
            if(const MStringView t = resolveForRole(ClassRoleKind::Subsystem); !t.empty())
                return t;
        }
        if(!scopedCls->systemTypeName.empty()){
            if(const MStringView t = resolveForRole(ClassRoleKind::System); !t.empty())
                return t;
        }
        if(!scopedCls->componentTypeName.empty()){
            if(const MStringView t = resolveForRole(ClassRoleKind::Component); !t.empty())
                return t;
        }
        return {};
    }
    if(const InterfaceDecl* scopedIface = findInterface(baseName); scopedIface && !scopedIface->assetTypeName.empty())
        return resolveForRole(ClassRoleKind::Asset);
    return {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

