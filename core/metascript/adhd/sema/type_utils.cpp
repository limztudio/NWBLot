// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "type_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_type_utils{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr BuiltinObjectTypeInfo s_BuiltinObjectTypes[] = {
#define NWB_ADHD_EXPORT_OBJECT_TYPE(adhdName, cppExportName, byteSize, byteAlign, isArenaBacked, isSelfDefaulted, isSmallValue, isStringSwitch, isNonThreadSafeContainer, isNwbMapKey) \
    BuiltinObjectTypeInfo{ adhdName, cppExportName, byteSize, byteAlign, isArenaBacked, isSelfDefaulted, isSmallValue, isStringSwitch, isNonThreadSafeContainer, isNwbMapKey },
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_OBJECT_TYPE
};

inline constexpr BuiltinGenericTypeInfo s_BuiltinGenericTypes[] = {
#define NWB_ADHD_EXPORT_GENERIC_TYPE(adhdName, cppExportName, paramKind, byteSize, byteAlign, isArenaBacked, isSelfDefaulted, isSmallValue, isNonThreadSafeContainer, isNonRandomAccessContainer, isIndexableSequence, isReflectedLinear, isReflectedMap) \
    BuiltinGenericTypeInfo{ adhdName, cppExportName, GenericParamKind::paramKind, byteSize, byteAlign, isArenaBacked, isSelfDefaulted, isSmallValue, isNonThreadSafeContainer, isNonRandomAccessContainer, isIndexableSequence, isReflectedLinear, isReflectedMap },
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_GENERIC_TYPE
};

inline constexpr BuiltinUnalignedVectorInfo s_BuiltinUnalignedVectorTypes[] = {
#define NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE(adhdName, cppExportName, lanes, byteSize, byteAlign, laneScalarType) \
    BuiltinUnalignedVectorInfo{ adhdName, cppExportName, laneScalarType, lanes, byteSize, byteAlign },
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE
};

inline constexpr BuiltinSimdVectorInfo s_BuiltinSimdVectorTypes[] = {
#define NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE(adhdName, cppExportName, lanes, byteSize, byteAlign, xyzScalarType, wScalarType, isFloatReg, isDoubleReg, isIntReg, isSignedInt, isMixed, regCppType, loadCppFunc, storeCppFunc) \
    BuiltinSimdVectorInfo{ adhdName, cppExportName, xyzScalarType, wScalarType, regCppType, loadCppFunc, storeCppFunc, lanes, byteSize, byteAlign, isFloatReg, isDoubleReg, isIntReg, isSignedInt, isMixed },
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE
};

inline constexpr BuiltinSimdMatrixInfo s_BuiltinSimdMatrixTypes[] = {
#define NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE(adhdName, cppExportName, rows, cols, elemCount, byteSize, byteAlign, elemScalarType, isDoubleReg, regCppType, loadCppFunc, storeCppFunc) \
    BuiltinSimdMatrixInfo{ adhdName, cppExportName, elemScalarType, regCppType, loadCppFunc, storeCppFunc, rows, cols, elemCount, byteSize, byteAlign, isDoubleReg },
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE
};

inline constexpr BuiltinMemberMethodInfo s_BuiltinMemberMethods[] = {
#define NWB_ADHD_EXPORT_MEMBER_METHOD(methodName, isMutating, adaptsFirstArgToElement, returnRule) \
    BuiltinMemberMethodInfo{ methodName, BuiltinMemberReturnRule::returnRule, isMutating, adaptsFirstArgToElement },
#include "../export_functions.h"
#undef NWB_ADHD_EXPORT_MEMBER_METHOD
};

inline constexpr BuiltinFunctionInfo s_BuiltinFunctions[] = {
#define NWB_ADHD_EXPORT_FUNCTION(adhdName, cppExportName, arity, returnRule) \
    BuiltinFunctionInfo{ adhdName, cppExportName, arity, BuiltinFunctionReturnRule::returnRule },
#include "../export_functions.h"
#undef NWB_ADHD_EXPORT_FUNCTION
};

inline constexpr BuiltinGlobalConstantInfo s_BuiltinGlobalConstants[] = {
#define NWB_ADHD_EXPORT_GLOBAL_CONSTANT(adhdName, cppExportName, adhdTypeName) \
    BuiltinGlobalConstantInfo{ adhdName, cppExportName, adhdTypeName },
#include "../export_functions.h"
#undef NWB_ADHD_EXPORT_GLOBAL_CONSTANT
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const BuiltinObjectTypeInfo* FindBuiltinObjectType(const MStringView name)noexcept{
    for(const BuiltinObjectTypeInfo& info : __hidden_adhd_type_utils::s_BuiltinObjectTypes){
        if(info.adhdName == name)
            return &info;
    }
    return nullptr;
}

const BuiltinGenericTypeInfo* FindBuiltinGenericType(const MStringView name)noexcept{
    for(const BuiltinGenericTypeInfo& info : __hidden_adhd_type_utils::s_BuiltinGenericTypes){
        if(info.adhdName == name)
            return &info;
    }
    return nullptr;
}

const BuiltinUnalignedVectorInfo* FindBuiltinUnalignedVectorType(const MStringView name)noexcept{
    for(const BuiltinUnalignedVectorInfo& info : __hidden_adhd_type_utils::s_BuiltinUnalignedVectorTypes){
        if(info.adhdName == name)
            return &info;
    }
    return nullptr;
}

const BuiltinSimdVectorInfo* FindBuiltinSimdVectorType(const MStringView name)noexcept{
    for(const BuiltinSimdVectorInfo& info : __hidden_adhd_type_utils::s_BuiltinSimdVectorTypes){
        if(info.adhdName == name)
            return &info;
    }
    return nullptr;
}

const BuiltinSimdMatrixInfo* FindBuiltinSimdMatrixType(const MStringView name)noexcept{
    for(const BuiltinSimdMatrixInfo& info : __hidden_adhd_type_utils::s_BuiltinSimdMatrixTypes){
        if(info.adhdName == name)
            return &info;
    }
    return nullptr;
}

b8 FindBuiltinFixedLayout(const MStringView name, usize& outSize, usize& outAlign)noexcept{
#define NWB_ADHD_EXPORT_SCALAR_TYPE(adhdName, byteSize, byteAlign, isInteger, isFixedWidthInt, isSigned, isFloat, defaultLiteral) \
    if(name == adhdName){ outSize = byteSize; outAlign = byteAlign; return true; }
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_SCALAR_TYPE
    if(const BuiltinObjectTypeInfo* objInfo = FindBuiltinObjectType(name)){
        outSize = objInfo->byteSize;
        outAlign = objInfo->byteAlign;
        return true;
    }
    if(const BuiltinUnalignedVectorInfo* unalignedInfo = FindBuiltinUnalignedVectorType(name)){
        outSize = unalignedInfo->byteSize;
        outAlign = unalignedInfo->byteAlign;
        return true;
    }
    if(const BuiltinSimdVectorInfo* vecInfo = FindBuiltinSimdVectorType(name)){
        outSize = vecInfo->byteSize;
        outAlign = vecInfo->byteAlign;
        return true;
    }
    if(const BuiltinSimdMatrixInfo* matInfo = FindBuiltinSimdMatrixType(name)){
        outSize = matInfo->byteSize;
        outAlign = matInfo->byteAlign;
        return true;
    }
    if(const BuiltinGenericTypeInfo* genericInfo = FindBuiltinGenericType(name); genericInfo && genericInfo->byteSize > 0u){
        outSize = genericInfo->byteSize;
        outAlign = genericInfo->byteAlign;
        return true;
    }
    return false;
}

const BuiltinMemberMethodInfo* FindBuiltinMemberMethod(const MStringView name)noexcept{
    for(const BuiltinMemberMethodInfo& info : __hidden_adhd_type_utils::s_BuiltinMemberMethods){
        if(info.methodName == name)
            return &info;
    }
    return nullptr;
}

const BuiltinFunctionInfo* FindBuiltinFunction(const MStringView name)noexcept{
    for(const BuiltinFunctionInfo& info : __hidden_adhd_type_utils::s_BuiltinFunctions){
        if(info.adhdName == name)
            return &info;
    }
    return nullptr;
}

const BuiltinGlobalConstantInfo* FindBuiltinGlobalConstant(const MStringView name)noexcept{
    for(const BuiltinGlobalConstantInfo& info : __hidden_adhd_type_utils::s_BuiltinGlobalConstants){
        if(info.adhdName == name)
            return &info;
    }
    return nullptr;
}

MStringView FindBuiltinGlobalConstantType(const MStringView name)noexcept{
    if(const BuiltinGlobalConstantInfo* info = FindBuiltinGlobalConstant(name))
        return info->adhdTypeName;
    return {};
}

MStringView FindBuiltinGlobalConstantCppName(const MStringView name)noexcept{
    if(const BuiltinGlobalConstantInfo* info = FindBuiltinGlobalConstant(name))
        return info->cppExportName;
    return {};
}

MStringView FindScalarDefaultLiteral(const MStringView name)noexcept{
#define NWB_ADHD_EXPORT_SCALAR_TYPE(adhdName, byteSize, byteAlign, isInteger, isFixedWidthInt, isSigned, isFloat, defaultLiteral) \
    if(name == adhdName) return defaultLiteral;
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_SCALAR_TYPE
    return {};
}

b8 IsNwbMapKeyType(const MStringView name)noexcept{
    if(const BuiltinObjectTypeInfo* info = FindBuiltinObjectType(name))
        return info->isNwbMapKey;
    return false;
}

b8 IsNonThreadSafeContainer(const MStringView typeName)noexcept{
    if(const BuiltinObjectTypeInfo* objInfo = FindBuiltinObjectType(typeName))
        return objInfo->isNonThreadSafeContainer;
    if(const BuiltinGenericTypeInfo* info = FindBuiltinGenericType(typeName))
        return info->isNonThreadSafeContainer;
    return false;
}

b8 IsSingleTypeParamBuiltinGeneric(const MStringView name)noexcept{
    if(const BuiltinGenericTypeInfo* info = FindBuiltinGenericType(name))
        return info->paramKind == GenericParamKind::SingleType;
    return false;
}

b8 IsTwoTypeParamBuiltinGeneric(const MStringView name)noexcept{
    if(const BuiltinGenericTypeInfo* info = FindBuiltinGenericType(name))
        return info->paramKind == GenericParamKind::TwoType;
    return false;
}

b8 IsTypeAndCountBuiltinGeneric(const MStringView name)noexcept{
    if(const BuiltinGenericTypeInfo* info = FindBuiltinGenericType(name))
        return info->paramKind == GenericParamKind::TypeAndCount;
    return false;
}

b8 CheckSwizzlePattern(const MStringView text, const u32 maxLanes, b8& outIsPattern, b8& outValidLanes)noexcept{
    outIsPattern = false;
    outValidLanes = true;
    if(text.empty() || text.size() > 4u)
        return false;

    b8 usesXYZW = false;
    b8 usesRGBA = false;
    u32 highestLane = 0u;

    for(usize i = 0u; i < text.size(); ++i){
        u32 lane = 0u;
        b8 isRgba = false;
        if(!TryMapSwizzleCharToLane(text[i], lane, &isRgba))
            return false;
        if(isRgba)
            usesRGBA = true;
        else
            usesXYZW = true;

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

b8 IsVoidTypeRef(const TypeRef* ref)noexcept{
    if(!ref)
        return true;
    return ref->name == "void"
        && ref->refMode == ParamPassingMode::None
        && !ref->isAuto
        && !ref->isFuncType
        && ref->templateArgs.empty();
}

b8 AreTypeRefsEquivalent(const TypeRef* a, const TypeRef* b, const SymbolTable& symbols)noexcept{
    return AreTypeRefsEquivalentWithSubst(a, b, {}, {}, symbols);
}

b8 AreTypeRefsEquivalentWithSubst(
    const TypeRef* actual,
    const TypeRef* pattern,
    const Span<const TemplateParamDecl> ifaceParams,
    const Span<const TypeArg> ifaceArgs,
    const SymbolTable& symbols
)noexcept{
    actual = symbols.unwrapTypeAlias(actual);
    pattern = symbols.unwrapTypeAlias(pattern);
    if(actual == pattern)
        return true;
    if(!actual || !pattern)
        return IsVoidTypeRef(actual) && IsVoidTypeRef(pattern);

    if(!ifaceParams.empty() && !ifaceArgs.empty() && pattern->templateArgs.empty() && !pattern->isFuncType){
        const usize substCount = ifaceParams.size() < ifaceArgs.size() ? ifaceParams.size() : ifaceArgs.size();
        for(usize k = 0u; k < substCount; ++k){
            if(!ifaceParams[k].constValueType && ifaceParams[k].name == pattern->name && ifaceArgs[k].type){
                const TypeRef* subst = symbols.unwrapTypeAlias(ifaceArgs[k].type);
                if(!subst)
                    return false;
                if(pattern->refMode == ParamPassingMode::None || subst->refMode == pattern->refMode)
                    return AreTypeRefsEquivalent(actual, subst, symbols);
                if(actual->refMode != pattern->refMode)
                    return false;
                TypeRef actualNormalized(*const_cast<TypeRef*>(actual));
                actualNormalized.refMode = subst->refMode;
                return AreTypeRefsEquivalent(&actualNormalized, subst, symbols);
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
        if(!AreConstantTemplateArgsEquivalent(ta.constantText, tb.constantText, symbols)
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

b8 AreTemplateArgListsEqual(const Span<const TypeArg> a, const Span<const TypeArg> b, const SymbolTable& symbols)noexcept{
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
            if(!AreConstantTemplateArgsEquivalent(lhsName, rhsName, symbols))
                return false;
        }
    }
    return true;
}

b8 DoParamListsMatch(const Span<const ParamDecl> a, const Span<const ParamDecl> b, const SymbolTable& symbols)noexcept{
    if(a.size() != b.size())
        return false;
    for(usize i = 0u; i < a.size(); ++i){
        if(a[i].mode != b[i].mode || !AreTypeRefsEquivalent(a[i].type, b[i].type, symbols))
            return false;
    }
    return true;
}

TypeRef* ExtractUnderlyingFuncType(TypeRef* type, const SymbolTable& symbols)noexcept{
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

b8 IsKnownConcreteTypeName(const MStringView name, const SymbolTable& symbols)noexcept{
    if(name == "void")
        return true;
    if(IsScalarType(name) || SymbolTable::isSimdType(name) || SymbolTable::isUnalignedVectorType(name) || symbols.hasTypeSymbol(name))
        return true;
    if(FindBuiltinObjectType(name) != nullptr)
        return true;
    if(const BuiltinGenericTypeInfo* genericInfo = FindBuiltinGenericType(name))
        return genericInfo->paramKind != GenericParamKind::NotNullType;
    return false;
}

b8 IsConcreteTypeRef(const TypeRef& type, const SymbolTable& symbols)noexcept{
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
            if(!IsConcreteConstTemplateArgText(arg.type->name, symbols) && !IsConcreteTypeRef(*arg.type, symbols))
                return false;
        }
        else if(!arg.constantText.empty()){
            if(!IsConcreteConstTemplateArgText(arg.constantText, symbols))
                return false;
        }
    }
    return true;
}

b8 IsConcreteTemplateArgList(
    const Span<const TemplateParamDecl> params,
    const Span<const TypeArg> args,
    const SymbolTable& symbols
)noexcept{
    if(params.size() != args.size())
        return false;
    for(usize i = 0u; i < params.size(); ++i){
        if(params[i].constValueType){
            const MStringView ctext = args[i].type ? args[i].type->name : args[i].constantText;
            if(!IsConcreteConstTemplateArgText(ctext, symbols))
                return false;
        }
        else{
            if(!args[i].type || !IsConcreteTypeRef(*args[i].type, symbols))
                return false;
        }
    }
    return true;
}

b8 TypeContainsArenaBackedContainer(
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

i32 ScoreCallableParams(
    const SymbolTable& symbols,
    const Span<const ParamDecl> params,
    const Span<const MStringView> argTypeNames,
    const Span<const u8> argIsMut
)noexcept{
    i32 score = 0;
    const usize count = params.size();
    for(usize i = 0u; i < count; ++i){
        if(i < argIsMut.size()){
            const b8 paramMut = params[i].mode == ParamPassingMode::Mut;
            if(paramMut == (argIsMut[i] != 0u))
                score += 8;
        }
        if(i < argTypeNames.size() && !argTypeNames[i].empty() && params[i].type){
            const MStringView paramBase = symbols.resolveBaseTypeName(params[i].type->name);
            const MStringView argBase = symbols.resolveBaseTypeName(argTypeNames[i]);
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

b8 IsReflectedLinearContainerType(const MStringView baseName)noexcept{
    if(const BuiltinGenericTypeInfo* info = FindBuiltinGenericType(baseName))
        return info->isReflectedLinear;
    return false;
}

b8 IsReflectedMapContainerType(const MStringView baseName)noexcept{
    if(const BuiltinGenericTypeInfo* info = FindBuiltinGenericType(baseName))
        return info->isReflectedMap;
    return false;
}

b8 IsAssetDependencyType(const TypeRef* const type, const SymbolTable& symbols, const u32 depth)noexcept{
    if(!type || depth > 8u)
        return false;
    const TypeRef* resolved = symbols.unwrapTypeAlias(type);
    if(!resolved)
        return false;
    const MStringView tname = resolved->name;
    if(tname == "AssetRef")
        return true;
    if(const ClassDecl* cls = symbols.findClass(tname)){
        for(const FieldDecl* f : cls->orderedFields){
            if(!f || !IsFieldReflected(*f) || !f->type)
                continue;
            const TypeRef* ft = symbols.substituteTypeParams(f->type, cls->templateParams, resolved);
            if(IsAssetDependencyType(ft, symbols, depth + 1u))
                return true;
        }
    }
    if((tname == "Optional" || IsReflectedLinearContainerType(tname))
        && !resolved->templateArgs.empty() && resolved->templateArgs[0].type)
    {
        return IsAssetDependencyType(resolved->templateArgs[0].type, symbols, depth + 1u);
    }
    if(IsReflectedMapContainerType(tname)
        && resolved->templateArgs.size() >= 2u && resolved->templateArgs[1].type)
    {
        return IsAssetDependencyType(resolved->templateArgs[1].type, symbols, depth + 1u);
    }
    return false;
}

b8 IsAssetDependencyField(const FieldDecl& field, const SymbolTable& symbols, const u32 depth)noexcept{
    return IsAssetDependencyType(field.type, symbols, depth);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

