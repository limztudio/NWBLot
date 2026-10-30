// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "symbol_table.h"
#include "type_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_symbol_table{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TypeRef* SubstituteAliasTypeRef(
    const TypeRef& origType,
    const MVector<TemplateParamDecl>& templateParams,
    const MVector<TypeArg>& templateArgs,
    MetaArena& arena
){
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
    cloned->funcReturnType = origType.funcReturnType
        ? SubstituteAliasTypeRef(*origType.funcReturnType, templateParams, templateArgs, arena)
        : nullptr;
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
                    subArg.constantText = templateArgs[p].type ? templateArgs[p].type->name : templateArgs[p].constantText;
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
                    subArg.constantText = templateArgs[p].type ? templateArgs[p].type->name : templateArgs[p].constantText;
                    break;
                }
            }
        }
        cloned->templateArgs.push_back(subArg);
    }
    return cloned;
}

[[nodiscard]] MethodDecl* SubstituteInterfaceMethod(
    const SymbolTable& symbols,
    MethodDecl* method,
    const b8 hasSubst,
    const MVector<TemplateParamDecl>& templateParams,
    const MVector<TypeArg>& templateArgs,
    MetaArena& arena
){
    if(!method || !hasSubst)
        return method;
    MethodDecl* cloned = AllocateAstNode<MethodDecl>(arena);
    cloned->access = method->access;
    cloned->name = method->name;
    cloned->returnType = method->returnType
        ? symbols.substituteTypeParams(*method->returnType, templateParams, templateArgs)
        : nullptr;
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

[[nodiscard]] i32 ScoreCandidateParams(
    const SymbolTable& symbols,
    const MVector<ParamDecl>& params,
    const usize* preferredArgCount,
    const MVector<MStringView>* argTypeNames,
    const MVector<b8>* argIsMut
)noexcept{
    i32 score = (!preferredArgCount || params.size() == *preferredArgCount) ? 64 : 0;
    score += ScoreCallableParams(symbols, params, argTypeNames, argIsMut);
    return score;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

MethodDecl* SymbolTable::findFreeFunction(
    const MStringView name,
    const usize* preferredArgCount,
    const MVector<MStringView>* argTypeNames,
    const MVector<b8>* argIsMut
)const noexcept{
    MethodDecl* bestMatch = nullptr;
    i32 bestScore = -1;
    for(MethodDecl* fn : m_freeFunctions){
        if(!fn || fn->name != name)
            continue;
        if(!argTypeNames && !argIsMut){
            if(!preferredArgCount || fn->params.size() == *preferredArgCount)
                return fn;
            if(!bestMatch)
                bestMatch = fn;
            continue;
        }
        const i32 score = __hidden_adhd_symbol_table::ScoreCandidateParams(*this, fn->params, preferredArgCount, argTypeNames, argIsMut);
        if(score > bestScore){
            bestScore = score;
            bestMatch = fn;
        }
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

MethodDecl* SymbolTable::findInterfaceMethod(
    const TypeRef& ifaceType,
    const MStringView methodName,
    const usize* preferredArgCount,
    const u32 depth,
    const MVector<MStringView>* argTypeNames,
    const MVector<b8>* argIsMut
)const noexcept{
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
            if(!argTypeNames && !argIsMut){
                if(!preferredArgCount || subMethod->params.size() == *preferredArgCount)
                    return subMethod;
                if(!bestMatch)
                    bestMatch = subMethod;
                continue;
            }
            const i32 score = __hidden_adhd_symbol_table::ScoreCandidateParams(*this, subMethod->params, preferredArgCount, argTypeNames, argIsMut);
            if(score > bestScore){
                bestScore = score;
                bestMatch = subMethod;
            }
        }
    }

    for(const TypeRef* baseType : iface->baseInterfaces){
        if(!baseType)
            continue;
        const TypeRef* subBase = hasSubst
            ? substituteTypeParams(*baseType, iface->templateParams, resolvedIface->templateArgs)
            : baseType;
        if(!subBase)
            continue;
        MethodDecl* inherited = findInterfaceMethod(*subBase, methodName, preferredArgCount, depth + 1u, argTypeNames, argIsMut);
        if(inherited){
            if(!argTypeNames && !argIsMut){
                if(!preferredArgCount || inherited->params.size() == *preferredArgCount)
                    return inherited;
                if(!bestMatch)
                    bestMatch = inherited;
                continue;
            }
            const i32 score = __hidden_adhd_symbol_table::ScoreCandidateParams(*this, inherited->params, preferredArgCount, argTypeNames, argIsMut);
            if(score > bestScore){
                bestScore = score;
                bestMatch = inherited;
            }
        }
    }

    return bestMatch;
}

void SymbolTable::collectAllInterfaceMethods(const TypeRef& ifaceType, MVector<MethodDecl*>& outMethods, const u32 depth)const{
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
        const TypeRef* subBase = hasSubst
            ? substituteTypeParams(*baseType, iface->templateParams, resolvedIface->templateArgs)
            : baseType;
        if(subBase)
            collectAllInterfaceMethods(*subBase, outMethods, depth + 1u);
    }

    for(MethodDecl* method : iface->methods){
        if(!method)
            continue;
        outMethods.push_back(__hidden_adhd_symbol_table::SubstituteInterfaceMethod(*this, method, hasSubst, iface->templateParams, resolvedIface->templateArgs, m_arena));
    }
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
        if(!aliasDecl->templateParams.empty() && !cur->templateArgs.empty()){
            cur = substituteTypeParams(
                *aliasDecl->aliasedType,
                aliasDecl->templateParams,
                cur->templateArgs
            );
        }
        else{
            cur = aliasDecl->aliasedType;
        }
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

TypeRef* SymbolTable::substituteTypeParams(
    const TypeRef& origType,
    const MVector<TemplateParamDecl>& templateParams,
    const MVector<TypeArg>& templateArgs
)const{
    return __hidden_adhd_symbol_table::SubstituteAliasTypeRef(origType, templateParams, templateArgs, m_arena);
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
    if(isUnalignedVectorType(baseName) || findEnum(baseName) != nullptr)
        return true;
#define NWB_ADHD_EXPORT_OBJECT_TYPE(adhdName, cppExportName, byteSize, byteAlign, isArenaBacked, isSelfDefaulted, isSmallValue, isStringSwitch, isNonThreadSafeContainer, isNwbMapKey) \
    if constexpr(isSelfDefaulted){ \
        if(baseName == adhdName) return true; \
    }
#define NWB_ADHD_EXPORT_GENERIC_TYPE(adhdName, cppExportName, paramKind, byteSize, byteAlign, isArenaBacked, isSelfDefaulted, isSmallValue, isNonThreadSafeContainer, isNonRandomAccessContainer, isIndexableSequence, isReflectedLinear, isReflectedMap) \
    if constexpr(isSelfDefaulted){ \
        if(baseName == adhdName) return true; \
    }
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_GENERIC_TYPE
#undef NWB_ADHD_EXPORT_OBJECT_TYPE
    return false;
}

b8 SymbolTable::isArenaBackedContainer(const MStringView typeName)noexcept{
#define NWB_ADHD_EXPORT_OBJECT_TYPE(adhdName, cppExportName, byteSize, byteAlign, isArenaBacked, isSelfDefaulted, isSmallValue, isStringSwitch, isNonThreadSafeContainer, isNwbMapKey) \
    if constexpr(isArenaBacked){ \
        if(typeName == adhdName) return true; \
    }
#define NWB_ADHD_EXPORT_GENERIC_TYPE(adhdName, cppExportName, paramKind, byteSize, byteAlign, isArenaBacked, isSelfDefaulted, isSmallValue, isNonThreadSafeContainer, isNonRandomAccessContainer, isIndexableSequence, isReflectedLinear, isReflectedMap) \
    if constexpr(isArenaBacked){ \
        if(typeName == adhdName) return true; \
    }
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_GENERIC_TYPE
#undef NWB_ADHD_EXPORT_OBJECT_TYPE
    return false;
}

b8 SymbolTable::isSimdVectorType(const MStringView typeName, u32& outLanes)noexcept{
#define NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE(adhdName, cppExportName, lanes, byteSize, byteAlign, xyzScalarType, wScalarType, isFloatReg, isDoubleReg, isIntReg, isSignedInt, isMixed, regCppType, loadCppFunc, storeCppFunc) \
    if(typeName == adhdName){ \
        outLanes = lanes; \
        return true; \
    }
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE
    return false;
}

b8 SymbolTable::isUnalignedVectorType(const MStringView typeName, u32& outLanes)noexcept{
#define NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE(adhdName, cppExportName, lanes, byteSize, byteAlign, laneScalarType) \
    if(typeName == adhdName){ \
        outLanes = lanes; \
        return true; \
    }
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE
    return false;
}

b8 SymbolTable::isUnalignedVectorType(const MStringView typeName)noexcept{
    u32 lanes = 0u;
    return isUnalignedVectorType(typeName, lanes);
}

b8 SymbolTable::isSimdMatrixType(const MStringView typeName)noexcept{
#define NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE(adhdName, cppExportName, rows, cols, elemCount, byteSize, byteAlign, elemScalarType, isDoubleReg, regCppType, loadCppFunc, storeCppFunc) \
    if(typeName == adhdName) return true;
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE
    return false;
}

b8 SymbolTable::isSimdType(const MStringView typeName)noexcept{
    u32 lanes = 0u;
    return isSimdVectorType(typeName, lanes) || isSimdMatrixType(typeName);
}

b8 SymbolTable::isStringSwitchType(const MStringView typeName)noexcept{
#define NWB_ADHD_EXPORT_OBJECT_TYPE(adhdName, cppExportName, byteSize, byteAlign, isArenaBacked, isSelfDefaulted, isSmallValue, isStringSwitch, isNonThreadSafeContainer, isNwbMapKey) \
    if constexpr(isStringSwitch){ \
        if(typeName == adhdName) return true; \
    }
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_OBJECT_TYPE
    return false;
}

MStringView SymbolTable::mapBuiltinCppType(const MStringView typeName)noexcept{
#define NWB_ADHD_EXPORT_OBJECT_TYPE(adhdName, cppExportName, byteSize, byteAlign, isArenaBacked, isSelfDefaulted, isSmallValue, isStringSwitch, isNonThreadSafeContainer, isNwbMapKey) \
    if constexpr(!MStringView(cppExportName).empty()){ \
        if(typeName == adhdName) return cppExportName; \
    }
#define NWB_ADHD_EXPORT_GENERIC_TYPE(adhdName, cppExportName, paramKind, byteSize, byteAlign, isArenaBacked, isSelfDefaulted, isSmallValue, isNonThreadSafeContainer, isNonRandomAccessContainer, isIndexableSequence, isReflectedLinear, isReflectedMap) \
    if constexpr(!MStringView(cppExportName).empty()){ \
        if(typeName == adhdName) return cppExportName; \
    }
#define NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE(adhdName, cppExportName, lanes, byteSize, byteAlign, laneScalarType) \
    if(typeName == adhdName) return cppExportName;
#define NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE(adhdName, cppExportName, lanes, byteSize, byteAlign, xyzScalarType, wScalarType, isFloatReg, isDoubleReg, isIntReg, isSignedInt, isMixed, regCppType, loadCppFunc, storeCppFunc) \
    if(typeName == adhdName) return cppExportName;
#define NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE(adhdName, cppExportName, rows, cols, elemCount, byteSize, byteAlign, elemScalarType, isDoubleReg, regCppType, loadCppFunc, storeCppFunc) \
    if(typeName == adhdName) return cppExportName;
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE
#undef NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE
#undef NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE
#undef NWB_ADHD_EXPORT_GENERIC_TYPE
#undef NWB_ADHD_EXPORT_OBJECT_TYPE
    return {};
}

b8 SymbolTable::hasTypeSymbol(const MStringView name)const noexcept{
    return findEnum(name) != nullptr
        || findInterface(name) != nullptr
        || findClass(name) != nullptr
        || findTypeAlias(name) != nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

