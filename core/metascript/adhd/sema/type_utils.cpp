// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/metascript/adhd/ast/builtin_type_semantics.h>

#include "type_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_type_utils{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr BuiltinObjectTypeInfo s_BuiltinObjectTypes[] = {
#define NWB_ADHD_EXPORT_OBJECT_TYPE(adhdName, cppExportName, byteSize, byteAlign, isArenaBacked, isSelfDefaulted, isSmallValue, isBorrowView, isBinarySerializable, isSingleValueCtor, emitsBraceFieldInit, isStringLike, isStringSwitch, isNonThreadSafeContainer, isNwbMapKey, requiresTCharAdaptation, arenaCtorMode, isCopyMoveConstructible, isAssignable, isAbstract, isPolymorphicBase, formatArgWrapKind, requiredHeader) \
    BuiltinObjectTypeInfo{ adhdName, cppExportName, requiredHeader, byteSize, byteAlign, isArenaBacked, isSelfDefaulted, isSmallValue, isBorrowView, isBinarySerializable, isSingleValueCtor, emitsBraceFieldInit, isStringLike, isStringSwitch, isNonThreadSafeContainer, isNwbMapKey, requiresTCharAdaptation, static_cast<u8>(arenaCtorMode), isCopyMoveConstructible, isAssignable, isAbstract, isPolymorphicBase, formatArgWrapKind },
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_OBJECT_TYPE
};

inline constexpr BuiltinEnumInfo s_BuiltinEnums[] = {
#define NWB_ADHD_EXPORT_BUILTIN_ENUM(adhdName, cppTypeName, cppScopeName, byteSize, byteAlign, isFlag, requiredHeader) \
    BuiltinEnumInfo{ adhdName, cppTypeName, cppScopeName, requiredHeader, byteSize, byteAlign, isFlag },
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_BUILTIN_ENUM
};

inline constexpr BuiltinEnumEntryInfo s_BuiltinEnumEntries[] = {
#define NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY(adhdEnumName, entryName) \
    BuiltinEnumEntryInfo{ adhdEnumName, entryName },
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY
};

inline constexpr BuiltinGenericTypeInfo s_BuiltinGenericTypes[] = {
#define NWB_ADHD_EXPORT_GENERIC_TYPE(adhdName, cppExportName, paramKind, inlineLayoutKind, byteSize, byteAlign, isArenaBacked, isSelfDefaulted, isSmallValue, isBorrowView, isBinarySerializable, isSingleValueCtor, emitsBraceFieldInit, isNonThreadSafeContainer, isNonRandomAccessContainer, isIndexableSequence, reflectedLinearKind, isReflectedMap, isAssetHandle, isCopyMoveConstructible, emitsFirstTypeArgOnlyInCpp, arenaFirstInCtor, requiredHeader) \
    BuiltinGenericTypeInfo{ adhdName, cppExportName, requiredHeader, GenericParamKind::paramKind, InlineLayoutKind::inlineLayoutKind, ReflectedLinearKind::reflectedLinearKind, byteSize, byteAlign, isArenaBacked, isSelfDefaulted, isSmallValue, isBorrowView, isBinarySerializable, isSingleValueCtor, emitsBraceFieldInit, isNonThreadSafeContainer, isNonRandomAccessContainer, isIndexableSequence, isReflectedMap, isAssetHandle, isCopyMoveConstructible, emitsFirstTypeArgOnlyInCpp, arenaFirstInCtor },
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_GENERIC_TYPE
};

struct BuiltinNonCopyableBaseInterfaceInfo{
    MStringView ifaceName;
    MStringView cppExportName;
};

inline constexpr BuiltinNonCopyableBaseInterfaceInfo s_NonCopyableBaseInterfaces[] = {
#define NWB_ADHD_EXPORT_NON_COPYABLE_BASE_INTERFACE(ifaceName, cppExportName) \
    BuiltinNonCopyableBaseInterfaceInfo{ ifaceName, cppExportName },
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_NON_COPYABLE_BASE_INTERFACE
};

inline constexpr MStringView s_ArenaObjectTypeNames[] = {
#define NWB_ADHD_EXPORT_ARENA_TYPE(adhdName) MStringView(adhdName),
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_ARENA_TYPE
};

struct PointerLikeGenericTypeEntry{
    MStringView adhdName;
    b8 allowsInterfaceArg;
    b8 isSmartPtrDeref;
};

inline constexpr PointerLikeGenericTypeEntry s_PointerLikeGenericTypes[] = {
#define NWB_ADHD_EXPORT_POINTER_LIKE_GENERIC_TYPE(adhdName, allowsInterfaceArg, isSmartPtrDeref) \
    PointerLikeGenericTypeEntry{ adhdName, allowsInterfaceArg, isSmartPtrDeref },
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_POINTER_LIKE_GENERIC_TYPE
};

inline constexpr MStringView s_SubsystemVectorContainerTypeNames[] = {
#define NWB_ADHD_EXPORT_SUBSYSTEM_VECTOR_CONTAINER_TYPE(adhdName) MStringView(adhdName),
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_SUBSYSTEM_VECTOR_CONTAINER_TYPE
};

inline constexpr MStringView s_LiteralConstObjectTypeNames[] = {
#define NWB_ADHD_EXPORT_LITERAL_CONST_OBJECT_TYPE(adhdName) MStringView(adhdName),
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_LITERAL_CONST_OBJECT_TYPE
};

inline constexpr BuiltinFormatArgWrapInfo s_FormatArgWraps[] = {
#define NWB_ADHD_EXPORT_FORMAT_ARG_WRAP(wrapKindId, wrapInDiagnosticFormat, cppPrefix, cppSuffix) \
    BuiltinFormatArgWrapInfo{ static_cast<u8>(wrapKindId), wrapInDiagnosticFormat, cppPrefix, cppSuffix },
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_FORMAT_ARG_WRAP
};

inline constexpr MStringView s_MoveOnlyTypeNames[] = {
#define NWB_ADHD_EXPORT_MOVE_ONLY_TYPE(typeName) MStringView(typeName),
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_MOVE_ONLY_TYPE
};

inline constexpr BuiltinBinaryHashTypeInfo s_BuiltinBinaryHashTypes[] = {
#define NWB_ADHD_EXPORT_BINARY_HASH_TYPE(adhdName, memberSuffix) \
    BuiltinBinaryHashTypeInfo{ adhdName, memberSuffix },
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_BINARY_HASH_TYPE
};

inline constexpr BuiltinNwbValueTypeInfo s_BuiltinNwbValueTypes[] = {
#define NWB_ADHD_EXPORT_NWB_VALUE_TYPE(adhdName, nwbCheckMethod, nwbAccessorMethod, valuePrefix, valueSuffix, directFieldPrefix, directFieldSuffix, optionalFieldPrefix, optionalFieldSuffix, requiresNameAdmission) \
    BuiltinNwbValueTypeInfo{ adhdName, nwbCheckMethod, nwbAccessorMethod, valuePrefix, valueSuffix, directFieldPrefix, directFieldSuffix, optionalFieldPrefix, optionalFieldSuffix, requiresNameAdmission },
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_NWB_VALUE_TYPE
};

inline constexpr BuiltinImplicitConversionInfo s_BuiltinImplicitConversions[] = {
#define NWB_ADHD_EXPORT_IMPLICIT_CONVERSION(fromTypeName, toTypeName, propagatesFirstTypeArg) \
    BuiltinImplicitConversionInfo{ fromTypeName, toTypeName, propagatesFirstTypeArg },
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_IMPLICIT_CONVERSION
};

inline constexpr BuiltinUnalignedVectorInfo s_BuiltinUnalignedVectorTypes[] = {
#define NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE(adhdName, cppExportName, lanes, altCtorArity, byteSize, byteAlign, laneScalarType, isHalfFloat) \
    BuiltinUnalignedVectorInfo{ adhdName, cppExportName, laneScalarType, lanes, altCtorArity, byteSize, byteAlign, isHalfFloat },
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE
};

inline constexpr BuiltinSimdVectorInfo s_BuiltinSimdVectorTypes[] = {
#define NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE(adhdName, cppExportName, lanes, altCtorArity, byteSize, byteAlign, xyzScalarType, wScalarType, isFloatReg, isDoubleReg, isIntReg, isSignedInt, isMixed, regCppType, loadCppFunc, storeCppFunc) \
    BuiltinSimdVectorInfo{ adhdName, cppExportName, xyzScalarType, wScalarType, regCppType, loadCppFunc, storeCppFunc, lanes, altCtorArity, byteSize, byteAlign, isFloatReg, isDoubleReg, isIntReg, isSignedInt, isMixed },
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE
};

inline constexpr BuiltinSimdMatrixInfo s_BuiltinSimdMatrixTypes[] = {
#define NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE(adhdName, cppExportName, elemCount, byteSize, byteAlign, elemScalarType, rowVectorType, isDoubleReg, regCppType, loadCppFunc, storeCppFunc) \
    BuiltinSimdMatrixInfo{ adhdName, cppExportName, elemScalarType, rowVectorType, regCppType, loadCppFunc, storeCppFunc, elemCount, byteSize, byteAlign, isDoubleReg },
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE
};

inline constexpr BuiltinMemberMethodInfo s_BuiltinMemberMethods[] = {
#define NWB_ADHD_EXPORT_MEMBER_METHOD(methodName, isMutating, firstArgIsMut, adaptsFirstArgToElement, adaptsSecondArgToElement, returnRule, returnTypeName) \
    BuiltinMemberMethodInfo{ methodName, returnTypeName, BuiltinMemberReturnRule::returnRule, isMutating, firstArgIsMut, adaptsFirstArgToElement, adaptsSecondArgToElement },
#include "../export_functions.h"
#undef NWB_ADHD_EXPORT_MEMBER_METHOD
};

inline constexpr BuiltinReceiverMethodInfo s_BuiltinReceiverMethods[] = {
#define NWB_ADHD_EXPORT_RECEIVER_METHOD(receiverTypeName, methodName, returnTypeName, synthesizeTypeRef) \
    BuiltinReceiverMethodInfo{ receiverTypeName, methodName, returnTypeName, "", "", "", synthesizeTypeRef, false, false },
#define NWB_ADHD_EXPORT_RECEIVER_METHOD_EX(receiverTypeName, methodName, returnTypeName, returnTypeArgName, returnErrorTypeName, synthesizeTypeRef) \
    BuiltinReceiverMethodInfo{ receiverTypeName, methodName, returnTypeName, returnTypeArgName, "", returnErrorTypeName, synthesizeTypeRef, false, false },
#define NWB_ADHD_EXPORT_RECEIVER_METHOD_MUT(receiverTypeName, methodName, returnTypeName, returnTypeArgName, returnErrorTypeName, synthesizeTypeRef, isMutating) \
    BuiltinReceiverMethodInfo{ receiverTypeName, methodName, returnTypeName, returnTypeArgName, "", returnErrorTypeName, synthesizeTypeRef, isMutating, false },
#define NWB_ADHD_EXPORT_RECEIVER_METHOD_REF(receiverTypeName, methodName, returnTypeName, returnTypeArgName, returnErrorTypeName, synthesizeTypeRef, isMutating) \
    BuiltinReceiverMethodInfo{ receiverTypeName, methodName, returnTypeName, returnTypeArgName, "", returnErrorTypeName, synthesizeTypeRef, isMutating, true },
#define NWB_ADHD_EXPORT_RECEIVER_METHOD_NESTED(receiverTypeName, methodName, returnTypeName, returnTypeArgName, returnNestedTypeArgName, returnErrorTypeName, synthesizeTypeRef, isMutating) \
    BuiltinReceiverMethodInfo{ receiverTypeName, methodName, returnTypeName, returnTypeArgName, returnNestedTypeArgName, returnErrorTypeName, synthesizeTypeRef, isMutating, false },
#include "../export_functions.h"
#undef NWB_ADHD_EXPORT_RECEIVER_METHOD_NESTED
#undef NWB_ADHD_EXPORT_RECEIVER_METHOD_REF
#undef NWB_ADHD_EXPORT_RECEIVER_METHOD_MUT
#undef NWB_ADHD_EXPORT_RECEIVER_METHOD_EX
#undef NWB_ADHD_EXPORT_RECEIVER_METHOD
};

inline constexpr BuiltinReceiverSignatureInfo s_BuiltinReceiverSignatures[] = {
#define NWB_ADHD_EXPORT_RECEIVER_SIGNATURE(receiverTypeName, methodName, arity, arg0Rule, arg1Rule, arg2Rule, arg3Rule, arg4Rule, arg5Rule, arg6Rule, arg7Rule, arg8Rule) \
    BuiltinReceiverSignatureInfo{ receiverTypeName, methodName, { BuiltinFunctionArgRule::arg0Rule, BuiltinFunctionArgRule::arg1Rule, BuiltinFunctionArgRule::arg2Rule, BuiltinFunctionArgRule::arg3Rule, BuiltinFunctionArgRule::arg4Rule, BuiltinFunctionArgRule::arg5Rule, BuiltinFunctionArgRule::arg6Rule, BuiltinFunctionArgRule::arg7Rule, BuiltinFunctionArgRule::arg8Rule }, arity },
#include "../export_functions.h"
#undef NWB_ADHD_EXPORT_RECEIVER_SIGNATURE
};

inline constexpr BuiltinFieldInfo s_BuiltinFields[] = {
#define NWB_ADHD_EXPORT_BUILTIN_FIELD(receiverTypeName, fieldName, fieldTypeName, elemTypeName, countText) \
    BuiltinFieldInfo{ receiverTypeName, fieldName, fieldTypeName, elemTypeName, countText },
#include "../export_functions.h"
#undef NWB_ADHD_EXPORT_BUILTIN_FIELD
};

inline constexpr BuiltinFunctionArgRuleInfo s_BuiltinFunctionArgRules[] = {
#define NWB_ADHD_EXPORT_FUNCTION_ARG_RULE(ruleName, expectedParamType, isMut, requiredExactType, requiresStringLike, requiresIntegerScalar, errorMessage) \
    BuiltinFunctionArgRuleInfo{ BuiltinFunctionArgRule::ruleName, expectedParamType, requiredExactType, errorMessage, isMut, requiresStringLike, requiresIntegerScalar },
#include "../export_functions.h"
#undef NWB_ADHD_EXPORT_FUNCTION_ARG_RULE
};

inline constexpr BuiltinVisitorRuleInfo s_BuiltinVisitorRules[] = {
#define NWB_ADHD_EXPORT_VISITOR_RULE(ruleName, valueType, keyType, mutableWithReceiver) \
    BuiltinVisitorRuleInfo{ BuiltinFunctionArgRule::ruleName, valueType, keyType, mutableWithReceiver },
#include "../export_functions.h"
#undef NWB_ADHD_EXPORT_VISITOR_RULE
};

static_assert(BuiltinFunctionArgRule::Any == 0u);

inline constexpr BuiltinFunctionInfo s_BuiltinFunctions[] = {
#define NWB_ADHD_EXPORT_FUNCTION(adhdName, cppExportName, arity, returnRule, returnTypeName, returnTypeArgName, arg0Rule, arg1Rule, arg2Rule) \
    BuiltinFunctionInfo{ adhdName, cppExportName, returnTypeName, returnTypeArgName, "", "", "", arity, arity, BuiltinFunctionReturnRule::returnRule, BuiltinCallLowering::None, false, false, false, false, false, false, false, { BuiltinFunctionArgRule::arg0Rule, BuiltinFunctionArgRule::arg1Rule, BuiltinFunctionArgRule::arg2Rule, BuiltinFunctionArgRule::Any, BuiltinFunctionArgRule::Any, BuiltinFunctionArgRule::Any, BuiltinFunctionArgRule::Any, BuiltinFunctionArgRule::Any, BuiltinFunctionArgRule::Any } },
#define NWB_ADHD_EXPORT_FUNCTION5(adhdName, cppExportName, arity, returnRule, returnTypeName, returnTypeArgName, arg0Rule, arg1Rule, arg2Rule, arg3Rule, arg4Rule) \
    BuiltinFunctionInfo{ adhdName, cppExportName, returnTypeName, returnTypeArgName, "", "", "", arity, arity, BuiltinFunctionReturnRule::returnRule, BuiltinCallLowering::None, false, false, false, false, false, false, false, { BuiltinFunctionArgRule::arg0Rule, BuiltinFunctionArgRule::arg1Rule, BuiltinFunctionArgRule::arg2Rule, BuiltinFunctionArgRule::arg3Rule, BuiltinFunctionArgRule::arg4Rule, BuiltinFunctionArgRule::Any, BuiltinFunctionArgRule::Any, BuiltinFunctionArgRule::Any, BuiltinFunctionArgRule::Any } },
#define NWB_ADHD_EXPORT_FUNCTION_EX(adhdName, cppExportName, minArity, arity, returnRule, returnTypeName, returnTypeArgName, returnNestedTypeArgName, returnErrorTypeName, requiredHeader, prependsScratchArena, isVariadic, wrapsFormatArgs, arg0Rule, arg1Rule, arg2Rule, arg3Rule, arg4Rule) \
    BuiltinFunctionInfo{ adhdName, cppExportName, returnTypeName, returnTypeArgName, returnNestedTypeArgName, returnErrorTypeName, requiredHeader, minArity, arity, BuiltinFunctionReturnRule::returnRule, BuiltinCallLowering::None, prependsScratchArena, isVariadic, wrapsFormatArgs, (BuiltinFunctionReturnRule::returnRule == BuiltinFunctionReturnRule::CallTemplateArg0), false, false, false, { BuiltinFunctionArgRule::arg0Rule, BuiltinFunctionArgRule::arg1Rule, BuiltinFunctionArgRule::arg2Rule, BuiltinFunctionArgRule::arg3Rule, BuiltinFunctionArgRule::arg4Rule, BuiltinFunctionArgRule::Any, BuiltinFunctionArgRule::Any, BuiltinFunctionArgRule::Any, BuiltinFunctionArgRule::Any } },
#define NWB_ADHD_EXPORT_FUNCTION_CALL(adhdName, cppExportName, minArity, arity, returnRule, returnTypeName, returnTypeArgName, returnNestedTypeArgName, returnErrorTypeName, requiredHeader, prependsScratchArena, isVariadic, wrapsFormatArgs, callLowering, requiresExplicitTypeArg, returnsBorrowedView, rejectsSimdStorage, arg0Rule, arg1Rule, arg2Rule, arg3Rule, arg4Rule, arg5Rule, arg6Rule, arg7Rule, arg8Rule) \
    BuiltinFunctionInfo{ adhdName, cppExportName, returnTypeName, returnTypeArgName, returnNestedTypeArgName, returnErrorTypeName, requiredHeader, minArity, arity, BuiltinFunctionReturnRule::returnRule, BuiltinCallLowering::callLowering, prependsScratchArena, isVariadic, wrapsFormatArgs, requiresExplicitTypeArg, returnsBorrowedView, rejectsSimdStorage, false, { BuiltinFunctionArgRule::arg0Rule, BuiltinFunctionArgRule::arg1Rule, BuiltinFunctionArgRule::arg2Rule, BuiltinFunctionArgRule::arg3Rule, BuiltinFunctionArgRule::arg4Rule, BuiltinFunctionArgRule::arg5Rule, BuiltinFunctionArgRule::arg6Rule, BuiltinFunctionArgRule::arg7Rule, BuiltinFunctionArgRule::arg8Rule } },
#define NWB_ADHD_EXPORT_FUNCTION_REF(adhdName, cppExportName, arity, returnRule, returnTypeName, returnTypeArgName, requiredHeader, arg0Rule, arg1Rule, arg2Rule) \
    BuiltinFunctionInfo{ adhdName, cppExportName, returnTypeName, returnTypeArgName, "", "", requiredHeader, arity, arity, BuiltinFunctionReturnRule::returnRule, BuiltinCallLowering::None, false, false, false, (BuiltinFunctionReturnRule::returnRule == BuiltinFunctionReturnRule::CallTemplateArg0), false, false, true, { BuiltinFunctionArgRule::arg0Rule, BuiltinFunctionArgRule::arg1Rule, BuiltinFunctionArgRule::arg2Rule, BuiltinFunctionArgRule::Any, BuiltinFunctionArgRule::Any, BuiltinFunctionArgRule::Any, BuiltinFunctionArgRule::Any, BuiltinFunctionArgRule::Any, BuiltinFunctionArgRule::Any } },
#define NWB_ADHD_EXPORT_FUNCTION_WIDE(adhdName, cppExportName, arity, returnTypeName, returnTypeArgName, requiredHeader, arg0Rule, arg1Rule, arg2Rule, arg3Rule, arg4Rule, arg5Rule, arg6Rule, arg7Rule, arg8Rule, arg9Rule, arg10Rule, arg11Rule, arg12Rule, arg13Rule, arg14Rule, arg15Rule, arg16Rule, arg17Rule, arg18Rule, arg19Rule) \
    BuiltinFunctionInfo{ adhdName, cppExportName, returnTypeName, returnTypeArgName, "", "", requiredHeader, arity, arity, BuiltinFunctionReturnRule::FixedType, BuiltinCallLowering::None, false, false, false, false, false, false, false, { BuiltinFunctionArgRule::arg0Rule, BuiltinFunctionArgRule::arg1Rule, BuiltinFunctionArgRule::arg2Rule, BuiltinFunctionArgRule::arg3Rule, BuiltinFunctionArgRule::arg4Rule, BuiltinFunctionArgRule::arg5Rule, BuiltinFunctionArgRule::arg6Rule, BuiltinFunctionArgRule::arg7Rule, BuiltinFunctionArgRule::arg8Rule, BuiltinFunctionArgRule::arg9Rule, BuiltinFunctionArgRule::arg10Rule, BuiltinFunctionArgRule::arg11Rule, BuiltinFunctionArgRule::arg12Rule, BuiltinFunctionArgRule::arg13Rule, BuiltinFunctionArgRule::arg14Rule, BuiltinFunctionArgRule::arg15Rule, BuiltinFunctionArgRule::arg16Rule, BuiltinFunctionArgRule::arg17Rule, BuiltinFunctionArgRule::arg18Rule, BuiltinFunctionArgRule::arg19Rule } },
#include "../export_functions.h"
#undef NWB_ADHD_EXPORT_FUNCTION_WIDE
#undef NWB_ADHD_EXPORT_FUNCTION_REF
#undef NWB_ADHD_EXPORT_FUNCTION_CALL
#undef NWB_ADHD_EXPORT_FUNCTION_EX
#undef NWB_ADHD_EXPORT_FUNCTION5
#undef NWB_ADHD_EXPORT_FUNCTION
};

inline constexpr BuiltinGlobalConstantInfo s_BuiltinGlobalConstants[] = {
#define NWB_ADHD_EXPORT_GLOBAL_CONSTANT(adhdName, cppExportName, adhdTypeName, requiredHeader) \
    BuiltinGlobalConstantInfo{ adhdName, cppExportName, adhdTypeName, requiredHeader },
#include "../export_functions.h"
#undef NWB_ADHD_EXPORT_GLOBAL_CONSTANT
};

inline constexpr BuiltinFunctionArgRule::Enum s_NumericScalarArgRules[] = {
#define NWB_ADHD_EXPORT_NUMERIC_SCALAR_ARG_RULE(ruleName) BuiltinFunctionArgRule::ruleName,
#include "../export_functions.h"
#undef NWB_ADHD_EXPORT_NUMERIC_SCALAR_ARG_RULE
};

inline constexpr BuiltinContainerArgConstraintInfo s_ContainerArgConstraints[] = {
#define NWB_ADHD_EXPORT_CONTAINER_ARG_CONSTRAINT(ruleName, elementTypeName, templateArgCount, isExactElementCheck, checksContiguousSequence, countText) \
    BuiltinContainerArgConstraintInfo{ BuiltinFunctionArgRule::ruleName, elementTypeName, templateArgCount, isExactElementCheck, checksContiguousSequence, countText },
#include "../export_functions.h"
#undef NWB_ADHD_EXPORT_CONTAINER_ARG_CONSTRAINT
};

inline constexpr BuiltinSpecialFunctionInfo s_SpecialFunctions[] = {
#define NWB_ADHD_EXPORT_SPECIAL_FUNCTION_SEMANTICS(adhdName, kind) \
    BuiltinSpecialFunctionInfo{ adhdName, SpecialFunctionKind::kind },
#include "../export_functions.h"
#undef NWB_ADHD_EXPORT_SPECIAL_FUNCTION_SEMANTICS
};

inline constexpr BuiltinSimdLaneZeroRuleInfo s_SimdLaneZeroRules[] = {
#define NWB_ADHD_EXPORT_SIMD_LANE_ZERO_RULE(adhdName, ruleKind) \
    BuiltinSimdLaneZeroRuleInfo{ adhdName, SimdLaneZeroRuleKind::ruleKind },
#include "../export_functions.h"
#undef NWB_ADHD_EXPORT_SIMD_LANE_ZERO_RULE
};

inline constexpr BuiltinSpecialMethodInfo s_SpecialMethods[] = {
#define NWB_ADHD_EXPORT_SPECIAL_METHOD_SEMANTICS(methodName, kind) \
    BuiltinSpecialMethodInfo{ methodName, SpecialMethodKind::kind },
#include "../export_functions.h"
#undef NWB_ADHD_EXPORT_SPECIAL_METHOD_SEMANTICS
};

template<usize N>
[[nodiscard]] constexpr b8 ContainsName(const MStringView (&table)[N], const MStringView name)noexcept{
    for(const MStringView entry : table){
        if(entry == name)
            return true;
    }
    return false;
}

template<typename EntryT, usize N>
[[nodiscard]] constexpr const EntryT* FindBuiltinByAdhdName(const EntryT (&table)[N], const MStringView name)noexcept{
    for(const EntryT& info : table){
        if(info.adhdName == name)
            return &info;
    }
    return nullptr;
}

template<typename EntryT, usize N>
[[nodiscard]] constexpr const EntryT* FindBuiltinByRule(const EntryT (&table)[N], const BuiltinFunctionArgRule::Enum rule)noexcept{
    for(const EntryT& info : table){
        if(info.rule == rule)
            return &info;
    }
    return nullptr;
}

template<typename ObjPred, typename GenericPred>
[[nodiscard]] b8 MatchObjectOrGenericType(const MStringView name, ObjPred&& objPred, GenericPred&& genericPred)noexcept{
    if(const BuiltinObjectTypeInfo* objInfo = FindBuiltinObjectType(name))
        return objPred(*objInfo);
    if(const BuiltinGenericTypeInfo* genericInfo = FindBuiltinGenericType(name))
        return genericPred(*genericInfo);
    return false;
}

[[nodiscard]] MStringView ResolveBuiltinScopePrefix(const MStringView scopeTypeName)noexcept{
    if(const BuiltinObjectTypeInfo* objInfo = FindBuiltinObjectType(scopeTypeName))
        return objInfo->cppExportName;
    if(const BuiltinEnumInfo* enumInfo = FindBuiltinEnum(scopeTypeName))
        return enumInfo->cppScopeName;
    return {};
}

template<typename EntryT, usize N>
[[nodiscard]] const EntryT* FindBuiltinScopedSymbol(
    const EntryT (&table)[N],
    const MStringView scopeTypeName,
    const MStringView memberName,
    const b8 allowArenaTemplateScope
)noexcept{
    if(memberName.empty())
        return nullptr;
    const MStringView prefix = ResolveBuiltinScopePrefix(scopeTypeName);
    if(prefix.empty())
        return nullptr;
    constexpr MStringView s_ArenaSuffix = ContractSemantics::s_CppArenaTemplateScopeSep;
    constexpr MStringView s_ScopeSep = ContractSemantics::s_CppScopeSeparator;
    for(const auto& info : table){
        if(!info.cppExportName.starts_with(prefix) || !info.cppExportName.ends_with(memberName))
            continue;
        if(info.cppExportName.size() == prefix.size() + s_ScopeSep.size() + memberName.size()
            && info.cppExportName.substr(prefix.size(), s_ScopeSep.size()) == s_ScopeSep
        ){
            return &info;
        }
        if(allowArenaTemplateScope
            && info.cppExportName.size() == prefix.size() + s_ArenaSuffix.size() + memberName.size()
            && info.cppExportName.substr(prefix.size(), s_ArenaSuffix.size()) == s_ArenaSuffix
        ){
            return &info;
        }
    }
    return nullptr;
}

[[nodiscard]] const BuiltinImplicitConversionInfo* FindImplicitConversion(const MStringView fromTypeName, const MStringView toTypeName)noexcept{
    for(const BuiltinImplicitConversionInfo& info : s_BuiltinImplicitConversions){
        if(info.fromTypeName == fromTypeName && info.toTypeName == toTypeName)
            return &info;
    }
    return nullptr;
}

[[nodiscard]] const BuiltinNonCopyableBaseInterfaceInfo* FindNonCopyableBaseInterface(const MStringView name)noexcept{
    for(const BuiltinNonCopyableBaseInterfaceInfo& entry : s_NonCopyableBaseInterfaces){
        if(entry.ifaceName == name)
            return &entry;
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
    return __hidden_adhd_type_utils::MatchObjectOrGenericType(
        name,
        [](const BuiltinObjectTypeInfo& info)noexcept{ return info.arenaCtorMode == 1u; },
        [](const BuiltinGenericTypeInfo& genericInfo)noexcept{ return genericInfo.arenaFirstInCtor; }
    );
}

b8 OmitsArenaInCtorType(const MStringView name)noexcept{
    if(const BuiltinObjectTypeInfo* info = FindBuiltinObjectType(name))
        return info->arenaCtorMode == 2u;
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
    const auto hasValidLaneArity = [argCount](const u32 lanes, const u32 altCtorArity, const b8 allowTwoArg)noexcept -> b8{
        return argCount == 0u
            || argCount == 1u
            || (allowTwoArg && argCount == 2u)
            || argCount == lanes
            || (altCtorArity > 0u && argCount == altCtorArity);
    };
    if(const BuiltinSimdVectorInfo* vecInfo = FindBuiltinSimdVectorType(baseTypeName))
        return hasValidLaneArity(vecInfo->lanes, vecInfo->altCtorArity, vecInfo->lanes == 4u);
    if(const BuiltinUnalignedVectorInfo* unalignedInfo = FindBuiltinUnalignedVectorType(baseTypeName))
        return hasValidLaneArity(unalignedInfo->lanes, unalignedInfo->altCtorArity, unalignedInfo->lanes == 4u && !unalignedInfo->isHalfFloat);
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

const BuiltinReceiverSignatureInfo* FindBuiltinReceiverSignature(const MStringView receiverTypeName, const MStringView methodName)noexcept{
    for(const BuiltinReceiverSignatureInfo& info : __hidden_adhd_type_utils::s_BuiltinReceiverSignatures){
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
    return __hidden_adhd_type_utils::FindBuiltinByRule(__hidden_adhd_type_utils::s_BuiltinFunctionArgRules, rule);
}

const BuiltinVisitorRuleInfo* FindBuiltinVisitorRule(const BuiltinFunctionArgRule::Enum rule)noexcept{
    return __hidden_adhd_type_utils::FindBuiltinByRule(__hidden_adhd_type_utils::s_BuiltinVisitorRules, rule);
}

const BuiltinFunctionInfo* FindBuiltinFunction(const MStringView name)noexcept{
    return __hidden_adhd_type_utils::FindBuiltinByAdhdName(__hidden_adhd_type_utils::s_BuiltinFunctions, name);
}

const BuiltinFunctionInfo* FindBuiltinScopedFunction(const MStringView scopeTypeName, const MStringView memberName)noexcept{
    return __hidden_adhd_type_utils::FindBuiltinScopedSymbol(__hidden_adhd_type_utils::s_BuiltinFunctions, scopeTypeName, memberName, true);
}

const BuiltinGlobalConstantInfo* FindBuiltinGlobalConstant(const MStringView name)noexcept{
    return __hidden_adhd_type_utils::FindBuiltinByAdhdName(__hidden_adhd_type_utils::s_BuiltinGlobalConstants, name);
}

const BuiltinGlobalConstantInfo* FindBuiltinScopedConstant(const MStringView scopeTypeName, const MStringView memberName)noexcept{
    return __hidden_adhd_type_utils::FindBuiltinScopedSymbol(__hidden_adhd_type_utils::s_BuiltinGlobalConstants, scopeTypeName, memberName, false);
}

b8 IsBorrowViewType(const MStringView name)noexcept{
    return __hidden_adhd_type_utils::MatchObjectOrGenericType(
        name,
        [](const BuiltinObjectTypeInfo& objInfo)noexcept{ return objInfo.isBorrowView; },
        [](const BuiltinGenericTypeInfo& info)noexcept{ return info.isBorrowView; }
    );
}

b8 IsNonSerializableBuiltinType(const MStringView name)noexcept{
    if(SymbolTable::IsSimdRegisterType(name))
        return true;
    return __hidden_adhd_type_utils::MatchObjectOrGenericType(
        name,
        [](const BuiltinObjectTypeInfo& objInfo)noexcept{ return !objInfo.isBinarySerializable; },
        [](const BuiltinGenericTypeInfo& info)noexcept{ return !info.isBinarySerializable; }
    );
}

b8 IsSingleValueCtorBuiltinType(const MStringView name)noexcept{
    return __hidden_adhd_type_utils::MatchObjectOrGenericType(
        name,
        [](const BuiltinObjectTypeInfo& objInfo)noexcept{ return objInfo.isSingleValueCtor; },
        [](const BuiltinGenericTypeInfo& info)noexcept{ return info.isSingleValueCtor; }
    );
}

b8 NeedsBraceDefaultFieldInit(const MStringView name)noexcept{
    return __hidden_adhd_type_utils::MatchObjectOrGenericType(
        name,
        [](const BuiltinObjectTypeInfo& objInfo)noexcept{ return objInfo.emitsBraceFieldInit; },
        [](const BuiltinGenericTypeInfo& info)noexcept{ return info.emitsBraceFieldInit; }
    );
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
    return rm && rm->returnTypeName == BuiltinTypeSemantics::ByteScalarTypeName();
}

b8 IsStringReceiverBorrowViewMethod(const MStringView receiverTypeName, const MStringView methodName)noexcept{
    if(!IsStringLikeType(receiverTypeName))
        return false;
    const BuiltinReceiverMethodInfo* rm = FindBuiltinReceiverMethod(receiverTypeName, methodName);
    return rm && rm->returnTypeName == BuiltinTypeSemantics::StringViewTypeName();
}

b8 IsBuiltinStringViewBorrowFunction(const MStringView name)noexcept{
    const BuiltinFunctionInfo* fn = FindBuiltinFunction(name);
    return fn && fn->returnsBorrowedView;
}

b8 IsContainerOrWrapperValueAccessMethod(const MStringView receiverTypeName, const MStringView methodName, const b8 includeStrings)noexcept{
    if(includeStrings && IsStringByteAccessorMethod(receiverTypeName, methodName))
        return true;
    const BuiltinGenericTypeInfo* genericInfo = FindBuiltinGenericType(receiverTypeName);
    if(!genericInfo || genericInfo->isBorrowView)
        return false;
    if(IsOptionalOrExpectedGenericType(receiverTypeName))
        return FindSpecialMethodSemantics(methodName) == SpecialMethodKind::UnwrapValue;
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
    return __hidden_adhd_type_utils::FindImplicitConversion(fromTypeName, toTypeName) != nullptr;
}

b8 DoesImplicitConversionPropagateFirstTypeArg(const MStringView fromTypeName, const MStringView toTypeName)noexcept{
    const BuiltinImplicitConversionInfo* info = __hidden_adhd_type_utils::FindImplicitConversion(fromTypeName, toTypeName);
    return info && info->propagatesFirstTypeArg;
}

b8 IsNonThreadSafeContainer(const MStringView typeName)noexcept{
    return __hidden_adhd_type_utils::MatchObjectOrGenericType(
        typeName,
        [](const BuiltinObjectTypeInfo& objInfo)noexcept{ return objInfo.isNonThreadSafeContainer; },
        [](const BuiltinGenericTypeInfo& info)noexcept{ return info.isNonThreadSafeContainer; }
    );
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

b8 IsOptionalOrExpectedGenericType(const MStringView name)noexcept{
    if(const BuiltinGenericTypeInfo* info = FindBuiltinGenericType(name))
        return info->inlineLayoutKind == InlineLayoutKind::Expected || info->inlineLayoutKind == InlineLayoutKind::Optional;
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
    return BuiltinTypeSemantics::IsVoidTypeName(ref->name) && ref->refMode == ParamPassingMode::None && !ref->isAuto && !ref->isFuncType && ref->templateArgs.empty();
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

b8 IsNonCopyableBaseInterfaceName(const MStringView name)noexcept{
    return __hidden_adhd_type_utils::FindNonCopyableBaseInterface(name) != nullptr;
}

MStringView FindNonCopyableBaseInterfaceCppName(const MStringView name)noexcept{
    const __hidden_adhd_type_utils::BuiltinNonCopyableBaseInterfaceInfo* entry = __hidden_adhd_type_utils::FindNonCopyableBaseInterface(name);
    return entry ? entry->cppExportName : name;
}

b8 IsMoveOnlyTypeName(const MStringView name)noexcept{
    return __hidden_adhd_type_utils::ContainsName(__hidden_adhd_type_utils::s_MoveOnlyTypeNames, name);
}

b8 HasBuiltinSimdRegisterField(const MStringView typeName)noexcept{
    for(const BuiltinFieldInfo& info : __hidden_adhd_type_utils::s_BuiltinFields){
        if(info.receiverTypeName == typeName && SymbolTable::IsSimdRegisterType(info.fieldTypeName))
            return true;
    }
    return false;
}

b8 IsOverAlignedBuiltinType(const MStringView typeName)noexcept{
    usize byteSize = 0u;
    usize byteAlign = 0u;
    return FindBuiltinFixedLayout(typeName, byteSize, byteAlign) && byteAlign >= 16u;
}

b8 IsStringBorrowViewType(const MStringView name)noexcept{
    return IsStringLikeType(name) && IsBorrowViewType(name);
}

b8 IsArenaObjectType(const MStringView name)noexcept{
    return __hidden_adhd_type_utils::ContainsName(__hidden_adhd_type_utils::s_ArenaObjectTypeNames, name);
}

b8 AllowsInterfaceTypeArg(const MStringView name)noexcept{
    for(const __hidden_adhd_type_utils::PointerLikeGenericTypeEntry& entry : __hidden_adhd_type_utils::s_PointerLikeGenericTypes){
        if(entry.allowsInterfaceArg && entry.adhdName == name)
            return true;
    }
    return false;
}

b8 IsSmartPointerDerefType(const MStringView name)noexcept{
    for(const __hidden_adhd_type_utils::PointerLikeGenericTypeEntry& entry : __hidden_adhd_type_utils::s_PointerLikeGenericTypes){
        if(entry.isSmartPtrDeref && entry.adhdName == name)
            return true;
    }
    return false;
}

b8 IsSubsystemVectorContainerType(const MStringView name)noexcept{
    return __hidden_adhd_type_utils::ContainsName(__hidden_adhd_type_utils::s_SubsystemVectorContainerTypeNames, name);
}

b8 IsLiteralConstObjectType(const MStringView name)noexcept{
    return __hidden_adhd_type_utils::ContainsName(__hidden_adhd_type_utils::s_LiteralConstObjectTypeNames, name);
}

const BuiltinFormatArgWrapInfo* FindFormatArgWrapInfo(const u8 wrapKindId)noexcept{
    for(const BuiltinFormatArgWrapInfo& info : __hidden_adhd_type_utils::s_FormatArgWraps){
        if(info.wrapKindId == wrapKindId)
            return &info;
    }
    return nullptr;
}

MStringView FindFloatSimdRegisterTypeName(const b8 isDouble)noexcept{
    for(const BuiltinSimdVectorInfo& info : __hidden_adhd_type_utils::s_BuiltinSimdVectorTypes){
        if(info.cppExportName == info.regCppType && info.isFloatReg && !info.isIntReg && !info.isMixed && info.isDoubleReg == isDouble)
            return info.adhdName;
    }
    return {};
}

MStringView FindIntSimdStorageVectorTypeName(const b8 isDouble, const b8 isSigned)noexcept{
    for(const BuiltinSimdVectorInfo& info : __hidden_adhd_type_utils::s_BuiltinSimdVectorTypes){
        if(info.isIntReg && !info.isMixed && info.lanes == 4u && info.isDoubleReg == isDouble && info.isSignedInt == isSigned)
            return info.adhdName;
    }
    return {};
}

const BuiltinContainerArgConstraintInfo* FindContainerArgConstraint(const BuiltinFunctionArgRule::Enum rule)noexcept{
    return __hidden_adhd_type_utils::FindBuiltinByRule(__hidden_adhd_type_utils::s_ContainerArgConstraints, rule);
}

b8 IsNumericScalarArgRule(const BuiltinFunctionArgRule::Enum rule)noexcept{
    for(const BuiltinFunctionArgRule::Enum entry : __hidden_adhd_type_utils::s_NumericScalarArgRules){
        if(entry == rule)
            return true;
    }
    return false;
}

SpecialFunctionKind::Enum FindSpecialFunctionKind(const MStringView name)noexcept{
    for(const BuiltinSpecialFunctionInfo& info : __hidden_adhd_type_utils::s_SpecialFunctions){
        if(info.adhdName == name)
            return info.kind;
    }
    return SpecialFunctionKind::None;
}

SimdLaneZeroRuleKind::Enum FindSimdLaneZeroRule(const MStringView name)noexcept{
    for(const BuiltinSimdLaneZeroRuleInfo& info : __hidden_adhd_type_utils::s_SimdLaneZeroRules){
        if(info.adhdName == name)
            return info.ruleKind;
    }
    return SimdLaneZeroRuleKind::None;
}

SpecialMethodKind::Enum FindSpecialMethodSemantics(const MStringView methodName)noexcept{
    for(const BuiltinSpecialMethodInfo& info : __hidden_adhd_type_utils::s_SpecialMethods){
        if(info.methodName == methodName)
            return info.kind;
    }
    return SpecialMethodKind::None;
}

b8 BuiltinTypeSemantics::IsReservedPrimitiveOrContainerTypeName(const MStringView name)noexcept{
    for(const SpecialBuiltinTypeInfo& info : s_SpecialBuiltinTypes){
        if(info.isReservedPrimitive && info.adhdName == name)
            return true;
    }
    if(IsScalarType(name))
        return true;
    if(FindBuiltinSimdVectorType(name) != nullptr || FindBuiltinSimdMatrixType(name) != nullptr || FindBuiltinUnalignedVectorType(name) != nullptr)
        return true;
    const BuiltinGenericTypeInfo* genericInfo = FindBuiltinGenericType(name);
    return genericInfo && genericInfo->requiredHeader.empty();
}

b8 BuiltinTypeSemantics::IsBuiltinTypeName(const MStringView name)noexcept{
    if(IsReservedPrimitiveOrContainerTypeName(name))
        return true;
    return FindBuiltinGenericType(name) != nullptr || FindBuiltinObjectType(name) != nullptr || FindBuiltinEnum(name) != nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

