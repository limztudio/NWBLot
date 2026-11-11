// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/metascript/adhd/ast/builtin_type_semantics.h>

#include "symbol_table.h"
#include "type_substitution.h"
#include "callable_overload_selector.h"
#include "type_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 SymbolTable::IsArenaBackedContainer(const MStringView typeName)noexcept{
    if(const BuiltinObjectTypeInfo* objInfo = FindBuiltinObjectType(typeName))
        return objInfo->isArenaBacked;
    if(const BuiltinGenericTypeInfo* info = FindBuiltinGenericType(typeName))
        return info->isArenaBacked;
    return false;
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
        if(CallableOverloadSelector::ConsiderScoredCallableCandidate(*this, preferredArgCount, argTypeNames, argIsMut, *fn, bestMatch, bestScore))
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
    return CallableOverloadSelector::FindScoredClassCallable(*this, classDecl, name, false, preferredArgCount, preferConst, requirePublic, argTypeNames, argIsMut);
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
    return TypeSubstitution::SubstituteAliasType(origType, templateParams, templateArgs, m_arena);
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

b8 SymbolTable::isSelfDefaultedType(const MStringView typeName, const ClassDecl* callerClass)const noexcept{
    if(const TypeAliasDecl* aliasDecl = findTypeAlias(typeName)){
        if(aliasDecl->aliasedType && aliasDecl->aliasedType->isFuncType)
            return false;
    }
    const MStringView baseName = resolveBaseTypeName(typeName);
    if(const ClassDecl* cls = findClass(baseName)){
        b8 hasAnyCtor = false;
        for(const MethodDecl* m : cls->methods){
            if(m && m->isConstructor){
                hasAnyCtor = true;
                if(m->params.empty() && (callerClass == cls || m->access == AccessSpecifier::Public))
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
        if(const BuiltinObjectTypeInfo* objInfo = FindBuiltinObjectType(baseName))
            return objInfo->isSelfDefaulted;
        if(const BuiltinGenericTypeInfo* info = FindBuiltinGenericType(baseName))
            return info->isSelfDefaulted;
    }
    return false;
}

b8 SymbolTable::isSmallValueType(MStringView typeName)const noexcept{
    typeName = resolveBaseTypeName(typeName);
    if(BuiltinTypeSemantics::IsScalarType(typeName) || IsSimdRegisterVectorType(typeName) || findEnum(typeName) != nullptr)
        return true;
    if(!hasTypeSymbol(typeName)){
        if(FindBuiltinEnum(typeName) != nullptr)
            return true;
        if(const BuiltinObjectTypeInfo* objInfo = FindBuiltinObjectType(typeName))
            return objInfo->isSmallValue;
        if(const BuiltinGenericTypeInfo* info = FindBuiltinGenericType(typeName))
            return info->isSmallValue;
    }
    return false;
}

b8 SymbolTable::hasTypeSymbol(const MStringView name)const noexcept{
    return findEnum(name) != nullptr || findInterface(name) != nullptr || findClass(name) != nullptr || findTypeAlias(name) != nullptr;
}

b8 SymbolTable::isAbstractBuiltinType(const MStringView name)const noexcept{
    const MStringView baseName = resolveBaseTypeName(name);
    if(hasTypeSymbol(baseName))
        return false;
    const BuiltinObjectTypeInfo* objInfo = FindBuiltinObjectType(baseName);
    return objInfo && objInfo->isAbstract;
}

b8 SymbolTable::isPolymorphicBaseBuiltinType(const MStringView name)const noexcept{
    const MStringView baseName = resolveBaseTypeName(name);
    if(hasTypeSymbol(baseName))
        return false;
    const BuiltinObjectTypeInfo* objInfo = FindBuiltinObjectType(baseName);
    return objInfo && objInfo->isPolymorphicBase;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

