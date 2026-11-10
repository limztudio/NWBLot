// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/metascript/adhd/ast/builtin_type_semantics.h>

#include "type_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_type_utils{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Array s_BuiltinObjectTypes = {
#define NWB_ADHD_EXPORT_OBJECT_TYPE(adhdName, cppExportName, byteSize, byteAlign, isArenaBacked, isSelfDefaulted, isSmallValue, isBorrowView, isBinarySerializable, isSingleValueCtor, emitsBraceFieldInit, isStringLike, isStringSwitch, isNonThreadSafeContainer, isNwbMapKey, requiresTCharAdaptation, arenaFirstInCtor, requiredHeader) \
    BuiltinObjectTypeInfo{ adhdName, cppExportName, requiredHeader, byteSize, byteAlign, isArenaBacked, isSelfDefaulted, isSmallValue, isBorrowView, isBinarySerializable, isSingleValueCtor, emitsBraceFieldInit, isStringLike, isStringSwitch, isNonThreadSafeContainer, isNwbMapKey, requiresTCharAdaptation, arenaFirstInCtor },
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_OBJECT_TYPE
};

inline constexpr Array s_BuiltinEnums = {
#define NWB_ADHD_EXPORT_BUILTIN_ENUM(adhdName, cppTypeName, cppScopeName, byteSize, byteAlign, isFlag, requiredHeader) \
    BuiltinEnumInfo{ adhdName, cppTypeName, cppScopeName, requiredHeader, byteSize, byteAlign, isFlag },
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_BUILTIN_ENUM
};

inline constexpr Array s_BuiltinEnumEntries = {
#define NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY(adhdEnumName, entryName) \
    BuiltinEnumEntryInfo{ adhdEnumName, entryName },
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY
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
#define NWB_ADHD_EXPORT_NWB_VALUE_TYPE(adhdName, nwbCheckMethod, nwbAccessorMethod, valuePrefix, valueSuffix, directFieldPrefix, directFieldSuffix, optionalFieldPrefix, optionalFieldSuffix, requiresNameAdmission) \
    BuiltinNwbValueTypeInfo{ adhdName, nwbCheckMethod, nwbAccessorMethod, valuePrefix, valueSuffix, directFieldPrefix, directFieldSuffix, optionalFieldPrefix, optionalFieldSuffix, requiresNameAdmission },
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
    BuiltinReceiverMethodInfo{ receiverTypeName, methodName, returnTypeName, "", "", synthesizeTypeRef },
#define NWB_ADHD_EXPORT_RECEIVER_METHOD_EX(receiverTypeName, methodName, returnTypeName, returnTypeArgName, returnErrorTypeName, synthesizeTypeRef) \
    BuiltinReceiverMethodInfo{ receiverTypeName, methodName, returnTypeName, returnTypeArgName, returnErrorTypeName, synthesizeTypeRef },
#include "../export_functions.h"
#undef NWB_ADHD_EXPORT_RECEIVER_METHOD_EX
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
    BuiltinFunctionInfo{ adhdName, cppExportName, returnTypeName, returnTypeArgName, "", "", "", arity, arity, BuiltinFunctionReturnRule::returnRule, false, false, false, { BuiltinFunctionArgRule::arg0Rule, BuiltinFunctionArgRule::arg1Rule, BuiltinFunctionArgRule::arg2Rule } },
#define NWB_ADHD_EXPORT_FUNCTION5(adhdName, cppExportName, arity, returnRule, returnTypeName, returnTypeArgName, arg0Rule, arg1Rule, arg2Rule, arg3Rule, arg4Rule) \
    BuiltinFunctionInfo{ adhdName, cppExportName, returnTypeName, returnTypeArgName, "", "", "", arity, arity, BuiltinFunctionReturnRule::returnRule, false, false, false, { BuiltinFunctionArgRule::arg0Rule, BuiltinFunctionArgRule::arg1Rule, BuiltinFunctionArgRule::arg2Rule, BuiltinFunctionArgRule::arg3Rule, BuiltinFunctionArgRule::arg4Rule } },
#define NWB_ADHD_EXPORT_FUNCTION_EX(adhdName, cppExportName, minArity, arity, returnRule, returnTypeName, returnTypeArgName, returnNestedTypeArgName, returnErrorTypeName, requiredHeader, prependsScratchArena, isVariadic, wrapsFormatArgs, arg0Rule, arg1Rule, arg2Rule, arg3Rule, arg4Rule) \
    BuiltinFunctionInfo{ adhdName, cppExportName, returnTypeName, returnTypeArgName, returnNestedTypeArgName, returnErrorTypeName, requiredHeader, minArity, arity, BuiltinFunctionReturnRule::returnRule, prependsScratchArena, isVariadic, wrapsFormatArgs, { BuiltinFunctionArgRule::arg0Rule, BuiltinFunctionArgRule::arg1Rule, BuiltinFunctionArgRule::arg2Rule, BuiltinFunctionArgRule::arg3Rule, BuiltinFunctionArgRule::arg4Rule } },
#include "../export_functions.h"
#undef NWB_ADHD_EXPORT_FUNCTION_EX
#undef NWB_ADHD_EXPORT_FUNCTION5
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

const BuiltinEnumInfo* FindBuiltinEnum(const MStringView name)noexcept{
    return __hidden_adhd_type_utils::FindBuiltinByAdhdName(__hidden_adhd_type_utils::s_BuiltinEnums, name);
}

b8 HasBuiltinEnumEntry(const MStringView enumName, const MStringView entryName)noexcept{
    for(const BuiltinEnumEntryInfo& info : __hidden_adhd_type_utils::s_BuiltinEnumEntries){
        if(info.adhdEnumName == enumName && info.entryName == entryName)
            return true;
    }
    return false;
}

b8 IsArenaFirstCtorType(const MStringView name)noexcept{
    if(const BuiltinObjectTypeInfo* info = FindBuiltinObjectType(name))
        return info->arenaFirstInCtor;
    return false;
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
    if(SymbolTable::IsSimdRegisterVectorType(baseTypeName))
        return argCount <= 4u;
    if(SymbolTable::IsSimdRegisterMatrixType(baseTypeName))
        return argCount == 0u || argCount == 1u || argCount == 3u || argCount == 4u || argCount == 9u || argCount == 12u || argCount == 16u;
    if(const BuiltinSimdVectorInfo* vecInfo = FindBuiltinSimdVectorType(baseTypeName)){
        return argCount == 0u
            || argCount == 1u
            || (vecInfo->lanes == 4u && argCount == 2u)
            || argCount == vecInfo->lanes
            || (vecInfo->altCtorArity > 0u && argCount == vecInfo->altCtorArity);
    }
    if(const BuiltinUnalignedVectorInfo* unalignedInfo = FindBuiltinUnalignedVectorType(baseTypeName)){
        return argCount == 0u
            || argCount == 1u
            || (unalignedInfo->lanes == 4u && !unalignedInfo->isHalfFloat && argCount == 2u)
            || argCount == unalignedInfo->lanes
            || (unalignedInfo->altCtorArity > 0u && argCount == unalignedInfo->altCtorArity);
    }
    if(const BuiltinSimdMatrixInfo* matInfo = FindBuiltinSimdMatrixType(baseTypeName))
        return argCount == 0u || argCount == 1u || argCount == matInfo->elemCount;
    return true;
}

b8 FindBuiltinFixedLayout(const MStringView name, usize& outSize, usize& outAlign)noexcept{
    if(const BuiltinScalarTypeInfo* scalarInfo = BuiltinTypeSemantics::FindBuiltinScalarType(name)){
        outSize = scalarInfo->byteSize;
        outAlign = scalarInfo->byteAlign;
        return true;
    }
    if(const BuiltinObjectTypeInfo* objInfo = FindBuiltinObjectType(name)){
        outSize = objInfo->byteSize;
        outAlign = objInfo->byteAlign;
        return true;
    }
    if(const BuiltinEnumInfo* enumInfo = FindBuiltinEnum(name)){
        outSize = enumInfo->byteSize;
        outAlign = enumInfo->byteAlign;
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
    if(SymbolTable::IsSimdRegisterType(name))
        return true;
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

b8 HasMutableStringElements(const MStringView name)noexcept{
    if(const BuiltinObjectTypeInfo* info = FindBuiltinObjectType(name))
        return info->isStringLike && info->isArenaBacked;
    return false;
}

b8 IsStringByteAccessorMethod(const MStringView receiverTypeName, const MStringView methodName)noexcept{
    if(!IsStringLikeType(receiverTypeName))
        return false;
    const BuiltinReceiverMethodInfo* rm = FindBuiltinReceiverMethod(receiverTypeName, methodName);
    return rm && rm->returnTypeName == "u8";
}

b8 IsStringReceiverBorrowViewMethod(const MStringView receiverTypeName, const MStringView methodName)noexcept{
    if(!IsStringLikeType(receiverTypeName))
        return false;
    const BuiltinReceiverMethodInfo* rm = FindBuiltinReceiverMethod(receiverTypeName, methodName);
    return rm && rm->returnTypeName == "StringView";
}

b8 IsBuiltinStringViewBorrowFunction(const MStringView name)noexcept{
    const BuiltinFunctionInfo* fn = FindBuiltinFunction(name);
    return fn && fn->returnRule == BuiltinFunctionReturnRule::FixedType && fn->returnTypeName == "StringView"
        && (fn->argRules[0] == BuiltinFunctionArgRule::StringView || fn->argRules[0] == BuiltinFunctionArgRule::TextView)
        && name != "DiagnosticEventNameFromCategory";
}

b8 IsContainerOrWrapperValueAccessMethod(const MStringView receiverTypeName, const MStringView methodName, const b8 includeStrings)noexcept{
    if(includeStrings && IsStringByteAccessorMethod(receiverTypeName, methodName))
        return true;
    const BuiltinGenericTypeInfo* genericInfo = FindBuiltinGenericType(receiverTypeName);
    if(!genericInfo || genericInfo->isBorrowView)
        return false;
    if(genericInfo->inlineLayoutKind == InlineLayoutKind::Optional || genericInfo->inlineLayoutKind == InlineLayoutKind::Expected)
        return methodName == "value";
    if(genericInfo->inlineLayoutKind == InlineLayoutKind::Atomic)
        return false;
    const BuiltinMemberMethodInfo* memberInfo = FindBuiltinMemberMethod(methodName);
    return memberInfo && !memberInfo->isMutating && memberInfo->returnRule == BuiltinMemberReturnRule::FirstTypeArg;
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




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

