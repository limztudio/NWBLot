// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "type_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_type_utils{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Array s_BuiltinObjectTypes = {
#define NWB_ADHD_EXPORT_OBJECT_TYPE(adhdName, cppExportName, byteSize, byteAlign, isArenaBacked, isSelfDefaulted, isSmallValue, isBorrowView, isBinarySerializable, isSingleValueCtor, emitsBraceFieldInit, isStringLike, isStringSwitch, isNonThreadSafeContainer, isNwbMapKey, requiresTCharAdaptation) \
    BuiltinObjectTypeInfo{ adhdName, cppExportName, byteSize, byteAlign, isArenaBacked, isSelfDefaulted, isSmallValue, isBorrowView, isBinarySerializable, isSingleValueCtor, emitsBraceFieldInit, isStringLike, isStringSwitch, isNonThreadSafeContainer, isNwbMapKey, requiresTCharAdaptation },
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_OBJECT_TYPE
};

inline constexpr Array s_BuiltinGenericTypes = {
#define NWB_ADHD_EXPORT_GENERIC_TYPE(adhdName, cppExportName, paramKind, inlineLayoutKind, byteSize, byteAlign, isArenaBacked, isSelfDefaulted, isSmallValue, isBorrowView, isBinarySerializable, isSingleValueCtor, emitsBraceFieldInit, isNonThreadSafeContainer, isNonRandomAccessContainer, isIndexableSequence, reflectedLinearKind, isReflectedMap, isAssetHandle) \
    BuiltinGenericTypeInfo{ adhdName, cppExportName, GenericParamKind::paramKind, InlineLayoutKind::inlineLayoutKind, ReflectedLinearKind::reflectedLinearKind, byteSize, byteAlign, isArenaBacked, isSelfDefaulted, isSmallValue, isBorrowView, isBinarySerializable, isSingleValueCtor, emitsBraceFieldInit, isNonThreadSafeContainer, isNonRandomAccessContainer, isIndexableSequence, isReflectedMap, isAssetHandle },
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_GENERIC_TYPE
};

inline constexpr Array s_BuiltinBinaryHashTypes = {
#define NWB_ADHD_EXPORT_BINARY_HASH_TYPE(adhdName, memberSuffix) \
    BuiltinBinaryHashTypeInfo{ adhdName, memberSuffix },
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_BINARY_HASH_TYPE
};

inline constexpr Array s_BuiltinNwbValueTypes = {
#define NWB_ADHD_EXPORT_NWB_VALUE_TYPE(adhdName, nwbAccessorMethod, valuePrefix, valueSuffix, directFieldPrefix, directFieldSuffix, optionalFieldPrefix, optionalFieldSuffix) \
    BuiltinNwbValueTypeInfo{ adhdName, nwbAccessorMethod, valuePrefix, valueSuffix, directFieldPrefix, directFieldSuffix, optionalFieldPrefix, optionalFieldSuffix },
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_NWB_VALUE_TYPE
};

inline constexpr Array s_BuiltinImplicitConversions = {
#define NWB_ADHD_EXPORT_IMPLICIT_CONVERSION(fromTypeName, toTypeName, propagatesFirstTypeArg) \
    BuiltinImplicitConversionInfo{ fromTypeName, toTypeName, propagatesFirstTypeArg },
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_IMPLICIT_CONVERSION
};

inline constexpr Array s_BuiltinUnalignedVectorTypes = {
#define NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE(adhdName, cppExportName, lanes, altCtorArity, byteSize, byteAlign, laneScalarType, isHalfFloat) \
    BuiltinUnalignedVectorInfo{ adhdName, cppExportName, laneScalarType, lanes, altCtorArity, byteSize, byteAlign, isHalfFloat },
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE
};

inline constexpr Array s_BuiltinSimdVectorTypes = {
#define NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE(adhdName, cppExportName, lanes, altCtorArity, byteSize, byteAlign, xyzScalarType, wScalarType, isFloatReg, isDoubleReg, isIntReg, isSignedInt, isMixed, regCppType, loadCppFunc, storeCppFunc) \
    BuiltinSimdVectorInfo{ adhdName, cppExportName, xyzScalarType, wScalarType, regCppType, loadCppFunc, storeCppFunc, lanes, altCtorArity, byteSize, byteAlign, isFloatReg, isDoubleReg, isIntReg, isSignedInt, isMixed },
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE
};

inline constexpr Array s_BuiltinSimdMatrixTypes = {
#define NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE(adhdName, cppExportName, elemCount, byteSize, byteAlign, elemScalarType, rowVectorType, isDoubleReg, regCppType, loadCppFunc, storeCppFunc) \
    BuiltinSimdMatrixInfo{ adhdName, cppExportName, elemScalarType, rowVectorType, regCppType, loadCppFunc, storeCppFunc, elemCount, byteSize, byteAlign, isDoubleReg },
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE
};

inline constexpr Array s_BuiltinMemberMethods = {
#define NWB_ADHD_EXPORT_MEMBER_METHOD(methodName, isMutating, firstArgIsMut, adaptsFirstArgToElement, adaptsSecondArgToElement, returnRule, returnTypeName) \
    BuiltinMemberMethodInfo{ methodName, returnTypeName, BuiltinMemberReturnRule::returnRule, isMutating, firstArgIsMut, adaptsFirstArgToElement, adaptsSecondArgToElement },
#include "../export_functions.h"
#undef NWB_ADHD_EXPORT_MEMBER_METHOD
};

inline constexpr Array s_BuiltinReceiverMethods = {
#define NWB_ADHD_EXPORT_RECEIVER_METHOD(receiverTypeName, methodName, returnTypeName, synthesizeTypeRef) \
    BuiltinReceiverMethodInfo{ receiverTypeName, methodName, returnTypeName, synthesizeTypeRef },
#include "../export_functions.h"
#undef NWB_ADHD_EXPORT_RECEIVER_METHOD
};

inline constexpr Array s_BuiltinFields = {
#define NWB_ADHD_EXPORT_BUILTIN_FIELD(receiverTypeName, fieldName, fieldTypeName, elemTypeName, countText) \
    BuiltinFieldInfo{ receiverTypeName, fieldName, fieldTypeName, elemTypeName, countText },
#include "../export_functions.h"
#undef NWB_ADHD_EXPORT_BUILTIN_FIELD
};

inline constexpr Array s_BuiltinFunctionArgRules = {
#define NWB_ADHD_EXPORT_FUNCTION_ARG_RULE(ruleName, expectedParamType, isMut, requiredExactType, requiresStringLike, requiresIntegerScalar, errorMessage) \
    BuiltinFunctionArgRuleInfo{ BuiltinFunctionArgRule::ruleName, expectedParamType, requiredExactType, errorMessage, isMut, requiresStringLike, requiresIntegerScalar },
#include "../export_functions.h"
#undef NWB_ADHD_EXPORT_FUNCTION_ARG_RULE
};

inline constexpr Array s_BuiltinFunctions = {
#define NWB_ADHD_EXPORT_FUNCTION(adhdName, cppExportName, arity, returnRule, returnTypeName, returnTypeArgName, arg0Rule, arg1Rule, arg2Rule) \
    BuiltinFunctionInfo{ adhdName, cppExportName, returnTypeName, returnTypeArgName, arity, BuiltinFunctionReturnRule::returnRule, { BuiltinFunctionArgRule::arg0Rule, BuiltinFunctionArgRule::arg1Rule, BuiltinFunctionArgRule::arg2Rule } },
#include "../export_functions.h"
#undef NWB_ADHD_EXPORT_FUNCTION
};

inline constexpr Array s_BuiltinGlobalConstants = {
#define NWB_ADHD_EXPORT_GLOBAL_CONSTANT(adhdName, cppExportName, adhdTypeName) \
    BuiltinGlobalConstantInfo{ adhdName, cppExportName, adhdTypeName },
#include "../export_functions.h"
#undef NWB_ADHD_EXPORT_GLOBAL_CONSTANT
};

template<typename EntryT, usize N>
[[nodiscard]] constexpr const EntryT* FindBuiltinByAdhdName(const Array<EntryT, N>& table, const MStringView name)noexcept{
    for(const EntryT& info : table){
        if(info.adhdName == name)
            return &info;
    }
    return nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const BuiltinObjectTypeInfo* FindBuiltinObjectType(const MStringView name)noexcept{
    return __hidden_adhd_type_utils::FindBuiltinByAdhdName(__hidden_adhd_type_utils::s_BuiltinObjectTypes, name);
}

const BuiltinGenericTypeInfo* FindBuiltinGenericType(const MStringView name)noexcept{
    return __hidden_adhd_type_utils::FindBuiltinByAdhdName(__hidden_adhd_type_utils::s_BuiltinGenericTypes, name);
}

b8 FindBuiltinBinaryHashMemberSuffix(const MStringView name, MStringView& outMemberSuffix)noexcept{
    if(const BuiltinBinaryHashTypeInfo* info = __hidden_adhd_type_utils::FindBuiltinByAdhdName(__hidden_adhd_type_utils::s_BuiltinBinaryHashTypes, name)){
        outMemberSuffix = info->memberSuffix;
        return true;
    }
    return false;
}

const BuiltinNwbValueTypeInfo* FindBuiltinNwbValueType(const MStringView name)noexcept{
    return __hidden_adhd_type_utils::FindBuiltinByAdhdName(__hidden_adhd_type_utils::s_BuiltinNwbValueTypes, name);
}

const BuiltinUnalignedVectorInfo* FindBuiltinUnalignedVectorType(const MStringView name)noexcept{
    return __hidden_adhd_type_utils::FindBuiltinByAdhdName(__hidden_adhd_type_utils::s_BuiltinUnalignedVectorTypes, name);
}

const BuiltinSimdVectorInfo* FindBuiltinSimdVectorType(const MStringView name)noexcept{
    return __hidden_adhd_type_utils::FindBuiltinByAdhdName(__hidden_adhd_type_utils::s_BuiltinSimdVectorTypes, name);
}

const BuiltinSimdMatrixInfo* FindBuiltinSimdMatrixType(const MStringView name)noexcept{
    return __hidden_adhd_type_utils::FindBuiltinByAdhdName(__hidden_adhd_type_utils::s_BuiltinSimdMatrixTypes, name);
}

b8 IsValidBuiltinVectorOrMatrixCtorArity(const MStringView baseTypeName, const usize argCount)noexcept{
    if(const BuiltinSimdVectorInfo* vecInfo = FindBuiltinSimdVectorType(baseTypeName)){
        return argCount == 0u
            || argCount == 1u
            || argCount == vecInfo->lanes
            || (vecInfo->altCtorArity > 0u && argCount == vecInfo->altCtorArity);
    }
    if(const BuiltinUnalignedVectorInfo* unalignedInfo = FindBuiltinUnalignedVectorType(baseTypeName)){
        return argCount == 0u
            || argCount == 1u
            || argCount == unalignedInfo->lanes
            || (unalignedInfo->altCtorArity > 0u && argCount == unalignedInfo->altCtorArity);
    }
    if(const BuiltinSimdMatrixInfo* matInfo = FindBuiltinSimdMatrixType(baseTypeName))
        return argCount == 0u || argCount == 1u || argCount == matInfo->elemCount;
    return true;
}

b8 FindBuiltinFixedLayout(const MStringView name, usize& outSize, usize& outAlign)noexcept{
    if(const BuiltinScalarTypeInfo* scalarInfo = FindBuiltinScalarType(name)){
        outSize = scalarInfo->byteSize;
        outAlign = scalarInfo->byteAlign;
        return true;
    }
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

const BuiltinReceiverMethodInfo* FindBuiltinReceiverMethod(const MStringView receiverTypeName, const MStringView methodName)noexcept{
    for(const BuiltinReceiverMethodInfo& info : __hidden_adhd_type_utils::s_BuiltinReceiverMethods){
        if(info.receiverTypeName == receiverTypeName && info.methodName == methodName)
            return &info;
    }
    return nullptr;
}

const BuiltinFieldInfo* FindBuiltinField(const MStringView receiverTypeName, const MStringView fieldName)noexcept{
    for(const BuiltinFieldInfo& info : __hidden_adhd_type_utils::s_BuiltinFields){
        if(info.receiverTypeName == receiverTypeName && info.fieldName == fieldName)
            return &info;
    }
    return nullptr;
}

const BuiltinFunctionArgRuleInfo* FindBuiltinFunctionArgRule(const BuiltinFunctionArgRule::Enum rule)noexcept{
    for(const BuiltinFunctionArgRuleInfo& info : __hidden_adhd_type_utils::s_BuiltinFunctionArgRules){
        if(info.rule == rule)
            return &info;
    }
    return nullptr;
}

const BuiltinFunctionInfo* FindBuiltinFunction(const MStringView name)noexcept{
    return __hidden_adhd_type_utils::FindBuiltinByAdhdName(__hidden_adhd_type_utils::s_BuiltinFunctions, name);
}

const BuiltinGlobalConstantInfo* FindBuiltinGlobalConstant(const MStringView name)noexcept{
    return __hidden_adhd_type_utils::FindBuiltinByAdhdName(__hidden_adhd_type_utils::s_BuiltinGlobalConstants, name);
}

b8 IsBorrowViewType(const MStringView name)noexcept{
    if(const BuiltinObjectTypeInfo* objInfo = FindBuiltinObjectType(name))
        return objInfo->isBorrowView;
    if(const BuiltinGenericTypeInfo* info = FindBuiltinGenericType(name))
        return info->isBorrowView;
    return false;
}

b8 IsNonSerializableBuiltinType(const MStringView name)noexcept{
    if(const BuiltinObjectTypeInfo* objInfo = FindBuiltinObjectType(name))
        return !objInfo->isBinarySerializable;
    if(const BuiltinGenericTypeInfo* info = FindBuiltinGenericType(name))
        return !info->isBinarySerializable;
    return false;
}

b8 IsSingleValueCtorBuiltinType(const MStringView name)noexcept{
    if(const BuiltinObjectTypeInfo* objInfo = FindBuiltinObjectType(name))
        return objInfo->isSingleValueCtor;
    if(const BuiltinGenericTypeInfo* info = FindBuiltinGenericType(name))
        return info->isSingleValueCtor;
    return false;
}

b8 NeedsBraceDefaultFieldInit(const MStringView name)noexcept{
    if(const BuiltinObjectTypeInfo* objInfo = FindBuiltinObjectType(name))
        return objInfo->emitsBraceFieldInit;
    if(const BuiltinGenericTypeInfo* info = FindBuiltinGenericType(name))
        return info->emitsBraceFieldInit;
    return false;
}

b8 IsStringLikeType(const MStringView name)noexcept{
    if(const BuiltinObjectTypeInfo* info = FindBuiltinObjectType(name))
        return info->isStringLike;
    return false;
}

b8 IsOwningStringType(const MStringView name)noexcept{
    if(const BuiltinObjectTypeInfo* info = FindBuiltinObjectType(name))
        return info->isStringLike && !info->isBorrowView;
    return false;
}

b8 IsNwbMapKeyType(const MStringView name)noexcept{
    if(const BuiltinObjectTypeInfo* info = FindBuiltinObjectType(name))
        return info->isNwbMapKey;
    return false;
}

b8 RequiresTCharAdaptation(const MStringView name)noexcept{
    if(const BuiltinObjectTypeInfo* info = FindBuiltinObjectType(name))
        return info->requiresTCharAdaptation;
    return false;
}

b8 IsImplicitlyConvertibleBuiltinType(const MStringView fromTypeName, const MStringView toTypeName)noexcept{
    for(const BuiltinImplicitConversionInfo& info : __hidden_adhd_type_utils::s_BuiltinImplicitConversions){
        if(info.fromTypeName == fromTypeName && info.toTypeName == toTypeName)
            return true;
    }
    return false;
}

b8 DoesImplicitConversionPropagateFirstTypeArg(const MStringView fromTypeName, const MStringView toTypeName)noexcept{
    for(const BuiltinImplicitConversionInfo& info : __hidden_adhd_type_utils::s_BuiltinImplicitConversions){
        if(info.fromTypeName == fromTypeName && info.toTypeName == toTypeName)
            return info.propagatesFirstTypeArg;
    }
    return false;
}

b8 IsNonThreadSafeContainer(const MStringView typeName)noexcept{
    if(const BuiltinObjectTypeInfo* objInfo = FindBuiltinObjectType(typeName))
        return objInfo->isNonThreadSafeContainer;
    if(const BuiltinGenericTypeInfo* info = FindBuiltinGenericType(typeName))
        return info->isNonThreadSafeContainer;
    return false;
}

b8 IsFixedArrayContainerType(const MStringView name)noexcept{
    if(const BuiltinGenericTypeInfo* info = FindBuiltinGenericType(name))
        return info->inlineLayoutKind == InlineLayoutKind::Array;
    return false;
}

b8 IsOptionalContainerType(const MStringView name)noexcept{
    if(const BuiltinGenericTypeInfo* info = FindBuiltinGenericType(name))
        return info->inlineLayoutKind == InlineLayoutKind::Optional;
    return false;
}

b8 IsAtomicGenericType(const MStringView name)noexcept{
    if(const BuiltinGenericTypeInfo* info = FindBuiltinGenericType(name))
        return info->inlineLayoutKind == InlineLayoutKind::Atomic;
    return false;
}

b8 IsAssetHandleType(const MStringView name)noexcept{
    if(const BuiltinGenericTypeInfo* info = FindBuiltinGenericType(name))
        return info->isAssetHandle;
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
    return ref->name == "void" && ref->refMode == ParamPassingMode::None && !ref->isAuto && !ref->isFuncType && ref->templateArgs.empty();
}

b8 AreTypeRefsEquivalentWithSubst(const TypeRef* actual, const TypeRef* pattern, const Span<const TemplateParamDecl> ifaceParams, const Span<const TypeArg> ifaceArgs, const SymbolTable& symbols)noexcept{
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

    if(actual->name != pattern->name || actual->refMode != pattern->refMode || actual->isAuto != pattern->isAuto || actual->isFuncType != pattern->isFuncType || actual->templateArgs.size() != pattern->templateArgs.size())
        return false;
    for(usize i = 0u; i < actual->templateArgs.size(); ++i){
        const TypeArg& ta = actual->templateArgs[i];
        const TypeArg& tb = pattern->templateArgs[i];
        if(!AreConstantTemplateArgsEquivalent(ta.constantText, tb.constantText, symbols) || !AreTypeRefsEquivalentWithSubst(ta.type, tb.type, ifaceParams, ifaceArgs, symbols))
            return false;
    }
    if(actual->isFuncType){
        if(!AreTypeRefsEquivalentWithSubst(actual->funcReturnType, pattern->funcReturnType, ifaceParams, ifaceArgs, symbols) || actual->funcParams.size() != pattern->funcParams.size())
            return false;
        for(usize i = 0u; i < actual->funcParams.size(); ++i){
            if(actual->funcParams[i].mode != pattern->funcParams[i].mode || !AreTypeRefsEquivalentWithSubst(actual->funcParams[i].type, pattern->funcParams[i].type, ifaceParams, ifaceArgs, symbols))
                return false;
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
            if(!AreConstantTemplateArgsEquivalent(a[i].constantOrTypeName(), b[i].constantOrTypeName(), symbols))
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
        if(const BuiltinGenericTypeInfo* genericInfo = FindBuiltinGenericType(type->name); genericInfo && genericInfo->paramKind != GenericParamKind::TwoType && !type->templateArgs.empty() && type->templateArgs[0].type){
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
    if(IsScalarType(name) || SymbolTable::IsSimdType(name) || SymbolTable::IsUnalignedVectorType(name) || symbols.hasTypeSymbol(name))
        return true;
    if(FindBuiltinObjectType(name) != nullptr)
        return true;
    if(FindBuiltinGenericType(name) != nullptr)
        return true;
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
    if(IsAssetHandleType(type.name))
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

b8 IsConcreteTemplateArgList(const Span<const TemplateParamDecl> params, const Span<const TypeArg> args, const SymbolTable& symbols)noexcept{
    if(params.size() != args.size())
        return false;
    for(usize i = 0u; i < params.size(); ++i){
        if(params[i].constValueType){
            if(!IsConcreteConstTemplateArgText(args[i].constantOrTypeName(), symbols))
                return false;
        }
        else{
            if(!args[i].type || !IsConcreteTypeRef(*args[i].type, symbols))
                return false;
        }
    }
    return true;
}

b8 TypeContainsArenaBackedContainer(const TypeRef& type, const SymbolTable& symbols, const u32 depth)noexcept{
    if(depth >= 8u || type.refMode != ParamPassingMode::None)
        return false;
    const TypeRef* unwrapped = symbols.unwrapTypeAlias(&type);
    if(!unwrapped || unwrapped->refMode != ParamPassingMode::None)
        return false;
    if(SymbolTable::IsArenaBackedContainer(unwrapped->name))
        return true;
    for(const TypeArg& targ : unwrapped->templateArgs){
        if(targ.type && TypeContainsArenaBackedContainer(*targ.type, symbols, depth + 1u))
            return true;
    }
    return false;
}

b8 ShouldPackSerializeClass(const Span<const FieldDecl* const> fields, const SymbolTable& symbols)noexcept{
    for(const FieldDecl* field : fields){
        if(!field || !field->type)
            continue;
        const TypeRef* resolved = symbols.unwrapTypeAlias(field->type);
        const MStringView baseName = resolved ? resolved->name : field->type->name;
        if(SymbolTable::IsSimdType(baseName) || (symbols.findClass(baseName) && !ShouldPackSerializeClass(symbols.findClass(baseName)->fields, symbols)))
            return false;
        if(IsFixedArrayContainerType(baseName) && resolved && !resolved->templateArgs.empty() && resolved->templateArgs[0].type){
            if(SymbolTable::IsSimdType(symbols.resolveBaseTypeName(resolved->templateArgs[0].type->name)))
                return false;
        }
    }
    return true;
}

i32 ScoreCallableParams(const SymbolTable& symbols, const Span<const ParamDecl> params, const Span<const MStringView> argTypeNames, const Span<const u8> argIsMut)noexcept{
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
            if(paramBase == argBase)
                score += 16;
            else if(IsImplicitlyConvertibleBuiltinType(argBase, paramBase))
                score += 8;
        }
    }
    return score;
}

b8 IsReflectedLinearContainerType(const MStringView baseName)noexcept{
    if(const BuiltinGenericTypeInfo* info = FindBuiltinGenericType(baseName))
        return info->reflectedLinearKind != ReflectedLinearKind::None;
    return false;
}

b8 IsReflectedMapContainerType(const MStringView baseName)noexcept{
    if(const BuiltinGenericTypeInfo* info = FindBuiltinGenericType(baseName))
        return info->isReflectedMap;
    return false;
}

const TypeRef* UnwrapOptionalFieldType(const TypeRef* const fieldType, const SymbolTable& symbols, b8& outIsOptional, MStringView& outBaseTypeName)noexcept{
    outIsOptional = false;
    outBaseTypeName = {};
    if(!fieldType)
        return nullptr;
    const TypeRef* resolvedFieldType = symbols.unwrapTypeAlias(fieldType);
    const MStringView outerName = resolvedFieldType ? resolvedFieldType->name : fieldType->name;
    outIsOptional = (
        IsOptionalContainerType(outerName)
        && resolvedFieldType
        && !resolvedFieldType->templateArgs.empty()
        && resolvedFieldType->templateArgs[0].type
    );
    const TypeRef* effectiveTypeRef = outIsOptional
        ? symbols.unwrapTypeAlias(resolvedFieldType->templateArgs[0].type)
        : resolvedFieldType;
    outBaseTypeName = effectiveTypeRef
        ? symbols.resolveBaseTypeName(effectiveTypeRef->name)
        : (outIsOptional ? symbols.resolveBaseTypeName(resolvedFieldType->templateArgs[0].type->name) : outerName);
    return effectiveTypeRef;
}

b8 IsAssetDependencyType(const TypeRef* const type, const SymbolTable& symbols, const u32 depth)noexcept{
    if(!type || depth > 8u)
        return false;
    const TypeRef* resolved = symbols.unwrapTypeAlias(type);
    if(!resolved)
        return false;
    const MStringView tname = resolved->name;
    if(IsAssetHandleType(tname))
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
    if((IsOptionalContainerType(tname) || IsReflectedLinearContainerType(tname)) && !resolved->templateArgs.empty() && resolved->templateArgs[0].type)
        return IsAssetDependencyType(resolved->templateArgs[0].type, symbols, depth + 1u);
    if(IsReflectedMapContainerType(tname) && resolved->templateArgs.size() >= 2u && resolved->templateArgs[1].type)
        return IsAssetDependencyType(resolved->templateArgs[1].type, symbols, depth + 1u);
    return false;
}

b8 TryResolveSubsystemFieldAssetType(const FieldDecl& field, const SymbolTable& symbols, MStringView& outAssetTypeName, b8& outIsVector)noexcept{
    outAssetTypeName = {};
    outIsVector = false;
    if(!field.type)
        return false;

    const TypeRef* resolved = symbols.unwrapTypeAlias(field.type);
    if(!resolved || resolved->refMode != ParamPassingMode::None)
        return false;

    const TypeRef* assetRefType = resolved;
    if(resolved->name == "Vector" && resolved->templateArgs.size() == 1u && resolved->templateArgs[0].type){
        outIsVector = true;
        assetRefType = symbols.unwrapTypeAlias(resolved->templateArgs[0].type);
    }
    if(assetRefType && IsAssetHandleType(assetRefType->name) && assetRefType->templateArgs.size() == 1u && assetRefType->templateArgs[0].type){
        if(const TypeRef* targetType = symbols.unwrapTypeAlias(assetRefType->templateArgs[0].type))
            outAssetTypeName = targetType->name;
    }
    return !outAssetTypeName.empty();
}

const TypeRef* DeduceTemplateParamTypeRef(const TypeRef* const paramType, const TypeRef* const argType, const MStringView tpName, const SymbolTable& symbols, const u32 depth)noexcept{
    if(!paramType || !argType || depth > 8u)
        return nullptr;
    if(paramType->name == tpName && paramType->templateArgs.empty())
        return argType;
    const TypeRef* up = symbols.unwrapTypeAlias(paramType);
    const TypeRef* ua = symbols.unwrapTypeAlias(argType);
    if(!up || !ua)
        return nullptr;
    if(up->name == tpName && up->templateArgs.empty())
        return ua;
    if(up->name == ua->name && up->templateArgs.size() == ua->templateArgs.size()){
        for(usize k = 0u; k < up->templateArgs.size(); ++k){
            if(const TypeRef* found = DeduceTemplateParamTypeRef(up->templateArgs[k].type, ua->templateArgs[k].type, tpName, symbols, depth + 1u))
                return found;
        }
    }
    else if(DoesImplicitConversionPropagateFirstTypeArg(ua->name, up->name) && up->templateArgs.size() == 1u && !ua->templateArgs.empty()){
        if(const TypeRef* found = DeduceTemplateParamTypeRef(up->templateArgs[0].type, ua->templateArgs[0].type, tpName, symbols, depth + 1u))
            return found;
    }
    return nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

