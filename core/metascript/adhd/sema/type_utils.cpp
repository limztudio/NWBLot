// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/metascript/adhd/ast/builtin_type_semantics.h>
#include <core/metascript/adhd/ast/operator_semantics.h>
#include <core/metascript/adhd/parser/decl_parser.h>
#include <core/metascript/arena_names.h>
#include <core/metascript/literal_decoder.h>

#include <global/filesystem.h>

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

static_assert(BuiltinFunctionArgRule::Any == 0u);
static_assert(BuiltinFunctionArgRule::StaticCount == sizeof(s_BuiltinFunctionArgRules) / sizeof(s_BuiltinFunctionArgRules[0]));

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

template<typename Fn>
[[nodiscard]] decltype(auto) WithJoinedKey(const MStringView a, const MStringView sep, const MStringView b, const Fn& fn)noexcept{
    MChar stackBuf[512];
    const usize totalLen = a.size() + sep.size() + b.size();
    if(totalLen <= sizeof(stackBuf)){
        if(!a.empty())
            FOUNDATION_MEMCPY(stackBuf, a.size(), a.data(), a.size());
        if(!sep.empty())
            FOUNDATION_MEMCPY(stackBuf + a.size(), sep.size(), sep.data(), sep.size());
        if(!b.empty())
            FOUNDATION_MEMCPY(stackBuf + a.size() + sep.size(), b.size(), b.data(), b.size());
        return fn(MStringView(stackBuf, totalLen));
    }
    Alloc::ScratchArena scratch(MetascriptArenaScope::s_ParserScratch);
    AString<Alloc::ScratchArena> heapBuf(scratch);
    heapBuf.reserve(totalLen);
    heapBuf.append(a.data(), a.size());
    heapBuf.append(sep.data(), sep.size());
    heapBuf.append(b.data(), b.size());
    return fn(MStringView(heapBuf.data(), heapBuf.size()));
}

class CoreExternRegistry final{
public:
    CoreExternRegistry()
        : m_arena(MetascriptArenaScope::s_ParserScratch)
        , m_sourceText(m_arena)
        , m_loadError(m_arena)
        , m_ownedStrings(m_arena)
        , m_objectTypes(m_arena)
        , m_enums(m_arena)
        , m_enumEntries(m_arena)
        , m_fields(m_arena)
        , m_receiverMethods(m_arena)
        , m_receiverSignatures(m_arena)
        , m_argRules(m_arena)
        , m_visitorRules(m_arena)
        , m_functions(m_arena)
        , m_globalConstants(m_arena)
        , m_objectTypeByName(m_arena)
        , m_enumByName(m_arena)
        , m_enumEntryByKey(m_arena)
        , m_fieldByKey(m_arena)
        , m_receiverMethodByKey(m_arena)
        , m_receiverSigByKey(m_arena)
        , m_functionByName(m_arena)
        , m_functionByCppName(m_arena)
        , m_globalConstantByName(m_arena)
        , m_globalConstantByCppName(m_arena)
        , m_simdFieldTypes(m_arena)
        , m_exactRuleByKey(m_arena)
        , m_visitorRuleByKey(m_arena)
    {
        load();
    }

    [[nodiscard]] MStringView loadError()const noexcept{ return m_loadError; }

    [[nodiscard]] const BuiltinObjectTypeInfo* findObjectType(const MStringView name)const noexcept{
        return FindInMap(m_objectTypes, m_objectTypeByName, name);
    }

    [[nodiscard]] const BuiltinEnumInfo* findEnum(const MStringView name)const noexcept{
        return FindInMap(m_enums, m_enumByName, name);
    }

    [[nodiscard]] b8 hasEnumEntry(const MStringView enumName, const MStringView entryName)const noexcept{
        return WithJoinedKey(enumName, MStringView("\n", 1u), entryName, [&](const MStringView key)noexcept{
            return m_enumEntryByKey.find(key) != m_enumEntryByKey.end();
        });
    }

    [[nodiscard]] const BuiltinFieldInfo* findField(const MStringView receiverTypeName, const MStringView fieldName)const noexcept{
        return FindPairInMap(m_fields, m_fieldByKey, receiverTypeName, fieldName);
    }

    [[nodiscard]] const BuiltinReceiverMethodInfo* findReceiverMethod(const MStringView receiverTypeName, const MStringView methodName)const noexcept{
        return FindPairInMap(m_receiverMethods, m_receiverMethodByKey, receiverTypeName, methodName);
    }

    [[nodiscard]] const BuiltinReceiverSignatureInfo* findReceiverSignature(const MStringView receiverTypeName, const MStringView methodName)const noexcept{
        return FindPairInMap(m_receiverSignatures, m_receiverSigByKey, receiverTypeName, methodName);
    }

    [[nodiscard]] const BuiltinFunctionArgRuleInfo* findArgRule(const BuiltinFunctionArgRule::Enum rule)const noexcept{
        if(static_cast<usize>(rule) < static_cast<usize>(BuiltinFunctionArgRule::StaticCount))
            return nullptr;
        const usize idx = static_cast<usize>(rule) - static_cast<usize>(BuiltinFunctionArgRule::StaticCount);
        return (idx < m_argRules.size()) ? &m_argRules[idx] : nullptr;
    }

    [[nodiscard]] const BuiltinVisitorRuleInfo* findVisitorRule(const BuiltinFunctionArgRule::Enum rule)const noexcept{
        for(const BuiltinVisitorRuleInfo& info : m_visitorRules){
            if(info.rule == rule)
                return &info;
        }
        return nullptr;
    }

    [[nodiscard]] const BuiltinFunctionInfo* findFunction(const MStringView name)const noexcept{
        return FindInMap(m_functions, m_functionByName, name);
    }

    [[nodiscard]] const BuiltinFunctionInfo* findScopedFunction(const MStringView scopeTypeName, const MStringView memberName)const noexcept{
        return FindScopedInMap(m_functions, m_functionByCppName, scopeTypeName, memberName, true);
    }

    [[nodiscard]] const BuiltinGlobalConstantInfo* findGlobalConstant(const MStringView name)const noexcept{
        return FindInMap(m_globalConstants, m_globalConstantByName, name);
    }

    [[nodiscard]] const BuiltinGlobalConstantInfo* findScopedConstant(const MStringView scopeTypeName, const MStringView memberName)const noexcept{
        return FindScopedInMap(m_globalConstants, m_globalConstantByCppName, scopeTypeName, memberName, false);
    }

    [[nodiscard]] b8 hasSimdRegisterField(const MStringView typeName)const noexcept{
        return m_simdFieldTypes.find(typeName) != m_simdFieldTypes.end();
    }


private:
    class PreludeIncludeResolver final : public IIncludeResolver{
    public:
        explicit PreludeIncludeResolver(CoreExternRegistry& owner)noexcept
            : m_owner(owner)
        {}


    public:
        virtual b8 resolveInclude(const MStringView includePath, const b8, MetaArena& arena, MString& outResolvedKey, MString& outContent)override{
            if(includePath.empty())
                return false;
            outResolvedKey.assign(includePath.data(), includePath.size());
            return m_owner.tryReadProjectFile(includePath, arena, outContent);
        }


    private:
        CoreExternRegistry& m_owner;
    };

    [[nodiscard]] b8 tryReadProjectFile(const MStringView relativePath, MetaArena& arena, MString& outText)const{
        const auto tryReadPath = [&](const MStringView pathText) -> b8{
            const TString<MetaArena> nativePath = ConvertString(arena, pathText);
            const ::Path<MetaArena> diskPath(arena, TStringView(nativePath.data(), nativePath.size()));
            outText.clear();
            return static_cast<b8>(ReadTextFile(diskPath, outText));
        };

#if defined(NWB_PROJECT_SOURCE_DIR)
        {
            const MStringView root = NWB_PROJECT_SOURCE_DIR;
            if(!root.empty()){
                MString fullPath(root.data(), root.size(), arena);
                if(fullPath.back() != '/' && fullPath.back() != '\\')
                    fullPath.push_back('/');
                fullPath.append(relativePath.data(), relativePath.size());
                if(tryReadPath(MStringView(fullPath.data(), fullPath.size())))
                    return true;
            }
        }
#endif
        return tryReadPath(relativePath);
    }

    template<typename InfoT>
    [[nodiscard]] static const InfoT* FindInMap(const MVector<InfoT>& items, const MStringMap<usize>& indexMap, const MStringView key)noexcept{
        const auto it = indexMap.find(key);
        return (it != indexMap.end()) ? &items[it->second] : nullptr;
    }

    template<typename InfoT>
    [[nodiscard]] static const InfoT* FindPairInMap(const MVector<InfoT>& items, const MStringMap<usize>& indexMap, const MStringView a, const MStringView b)noexcept{
        return WithJoinedKey(a, MStringView("\n", 1u), b, [&](const MStringView key)noexcept -> const InfoT*{
            return FindInMap(items, indexMap, key);
        });
    }

    template<typename InfoT>
    [[nodiscard]] static const InfoT* FindScopedInMap(
        const MVector<InfoT>& items,
        const MStringMap<usize>& cppMap,
        const MStringView scopeTypeName,
        const MStringView memberName,
        const b8 allowArenaTemplateScope
    )noexcept{
        if(memberName.empty())
            return nullptr;
        const MStringView prefix = ResolveBuiltinScopePrefix(scopeTypeName);
        if(prefix.empty())
            return nullptr;
        if(const InfoT* direct = WithJoinedKey(prefix, ContractSemantics::s_CppScopeSeparator, memberName, [&](const MStringView key)noexcept{
            return FindInMap(items, cppMap, key);
        }))
            return direct;
        if(allowArenaTemplateScope){
            return WithJoinedKey(prefix, ContractSemantics::s_CppArenaTemplateScopeSep, memberName, [&](const MStringView key)noexcept{
                return FindInMap(items, cppMap, key);
            });
        }
        return nullptr;
    }

    [[nodiscard]] MStringView ownString(const MStringView text){
        if(text.empty())
            return {};
        const char* const srcBegin = m_sourceText.data();
        const char* const srcEnd = srcBegin + m_sourceText.size();
        if(text.data() >= srcBegin && text.data() + text.size() <= srcEnd)
            return text;
        m_ownedStrings.push_back(MakeGlobalUnique<MString>(m_arena, text.data(), text.size(), m_arena));
        const MString& stored = *m_ownedStrings.back();
        return MStringView(stored.data(), stored.size());
    }

    [[nodiscard]] MStringView allocOwnedString(MString&& text){
        if(text.empty())
            return {};
        m_ownedStrings.push_back(MakeGlobalUnique<MString>(m_arena, Move(text)));
        const MString& stored = *m_ownedStrings.back();
        return MStringView(stored.data(), stored.size());
    }

    [[nodiscard]] MString buildPairKey(const MStringView a, const MStringView b){
        MString key(m_arena);
        key.reserve(a.size() + 1u + b.size());
        key.append(a.data(), a.size());
        key.push_back('\n');
        key.append(b.data(), b.size());
        return key;
    }

    void insertUniqueIndex(MStringMap<usize>& map, const MStringView key, const usize idx){
        if(!key.empty() && map.find(key) == map.end())
            map.emplace(MString(key.data(), key.size(), m_arena), idx);
    }

    void insertUniqueKey(MStringMap<usize>& map, MString&& key, const usize idx){
        const MStringView view(key.data(), key.size());
        if(!view.empty() && map.find(view) == map.end())
            map.emplace(Move(key), idx);
    }

    [[nodiscard]] BuiltinFunctionArgRule::Enum allocRuleId()const noexcept{
        return static_cast<BuiltinFunctionArgRule::Enum>(
            static_cast<usize>(BuiltinFunctionArgRule::StaticCount) + m_argRules.size()
        );
    }

    [[nodiscard]] BuiltinFunctionArgRule::Enum resolveArgRule(const ParamDecl& param){
        if(!param.type)
            return BuiltinFunctionArgRule::Any;
        const TypeRef& type = *param.type;
        const b8 isMut = (param.mode == ParamPassingMode::Mut);

        if(type.isFuncType){
            MStringView keyType;
            MStringView valueType;
            b8 mutableWithReceiver = false;
            if(type.funcParams.size() == 2u){
                keyType = ownString(type.funcParams[0].type ? type.funcParams[0].type->name : MStringView{});
                valueType = ownString(type.funcParams[1].type ? type.funcParams[1].type->name : MStringView{});
                mutableWithReceiver = (type.funcParams[1].mode == ParamPassingMode::Mut);
            }
            else if(type.funcParams.size() == 1u){
                valueType = ownString(type.funcParams[0].type ? type.funcParams[0].type->name : MStringView{});
                mutableWithReceiver = (type.funcParams[0].mode == ParamPassingMode::Mut);
            }
            MString vKey(m_arena);
            vKey.append(keyType.data(), keyType.size());
            vKey.push_back('\n');
            vKey.append(valueType.data(), valueType.size());
            vKey.push_back('\n');
            vKey.push_back(mutableWithReceiver ? '1' : '0');
            if(const auto it = m_visitorRuleByKey.find(MStringView(vKey.data(), vKey.size())); it != m_visitorRuleByKey.end())
                return it->second;
            const BuiltinFunctionArgRule::Enum ruleId = allocRuleId();
            m_argRules.push_back(BuiltinFunctionArgRuleInfo{ ruleId, {}, {}, {}, false, false, false });
            m_visitorRules.push_back(BuiltinVisitorRuleInfo{ ruleId, valueType, keyType, mutableWithReceiver });
            m_visitorRuleByKey.emplace(Move(vKey), ruleId);
            return ruleId;
        }

        if(type.isAuto)
            return isMut ? BuiltinFunctionArgRule::MutAny : BuiltinFunctionArgRule::Any;

        const MStringView tname = type.name;
        if(!isMut && tname == BuiltinTypeSemantics::StringViewTypeName())
            return BuiltinFunctionArgRule::TextView;
        if(isMut && tname == "String")
            return BuiltinFunctionArgRule::MutString;
        if(isMut && tname == "CompactString")
            return BuiltinFunctionArgRule::MutTextBuffer;
        if(isMut && tname == BuiltinTypeSemantics::UInt32ScalarTypeName())
            return BuiltinFunctionArgRule::MutU32;
        if(!isMut && tname == BuiltinTypeSemantics::UInt32ScalarTypeName())
            return BuiltinFunctionArgRule::U32Scalar;
        if(!isMut && tname == BuiltinTypeSemantics::Int64ScalarTypeName())
            return BuiltinFunctionArgRule::I64Scalar;
        if(isMut && tname == BuiltinTypeSemantics::UInt64ScalarTypeName())
            return BuiltinFunctionArgRule::MutU64;
        if(!isMut && tname == BuiltinTypeSemantics::UInt64ScalarTypeName())
            return BuiltinFunctionArgRule::U64Scalar;
        if(isMut && tname == BuiltinTypeSemantics::UsizeScalarTypeName())
            return BuiltinFunctionArgRule::MutUsize;
        if(isMut && tname == BuiltinTypeSemantics::Float32ScalarTypeName())
            return BuiltinFunctionArgRule::MutF32Scalar;
        if(!isMut && tname == BuiltinTypeSemantics::Float32ScalarTypeName())
            return BuiltinFunctionArgRule::F32Scalar;
        if(isMut && tname == BuiltinTypeSemantics::Float64ScalarTypeName())
            return BuiltinFunctionArgRule::MutF64Scalar;
        if(!isMut && tname == BuiltinTypeSemantics::Float64ScalarTypeName())
            return BuiltinFunctionArgRule::F64Scalar;
        if(!isMut && tname == BuiltinTypeSemantics::BoolScalarTypeName())
            return BuiltinFunctionArgRule::BoolScalar;
        if(!isMut && tname == "isize")
            return BuiltinFunctionArgRule::Integer;
        if(!isMut && tname == BuiltinTypeSemantics::NameTypeName())
            return BuiltinFunctionArgRule::NameValue;
        if(!isMut && tname == BuiltinTypeSemantics::PathTypeName())
            return BuiltinFunctionArgRule::PathValue;
        if(tname == BuiltinTypeSemantics::VectorSingleTypeName())
            return isMut ? BuiltinFunctionArgRule::MutVectorSingleArg : BuiltinFunctionArgRule::VectorSingleArg;
        if(tname == BuiltinTypeSemantics::VectorDoubleTypeName())
            return isMut ? BuiltinFunctionArgRule::MutVectorDoubleArg : BuiltinFunctionArgRule::VectorDoubleArg;
        if(tname == "MatrixSingle")
            return isMut ? BuiltinFunctionArgRule::MutMatrixSingleArg : BuiltinFunctionArgRule::MatrixSingleArg;
        if(tname == "MatrixDouble")
            return isMut ? BuiltinFunctionArgRule::MutMatrixDoubleArg : BuiltinFunctionArgRule::MatrixDoubleArg;
        if(tname == "Span" && type.templateArgs.size() == 1u && type.templateArgs[0].type){
            const MStringView elemName = type.templateArgs[0].type->name;
            if(elemName == BuiltinTypeSemantics::ByteScalarTypeName())
                return isMut ? BuiltinFunctionArgRule::MutableByteSequence : BuiltinFunctionArgRule::ReadableByteSequence;
            if(isMut && elemName == BuiltinTypeSemantics::UInt32ScalarTypeName())
                return BuiltinFunctionArgRule::MutableU32Sequence;
            if(isMut && elemName == BuiltinTypeSemantics::UInt64ScalarTypeName())
                return BuiltinFunctionArgRule::MutableU64Sequence;
            if(!isMut && elemName == BuiltinTypeSemantics::UsizeScalarTypeName())
                return BuiltinFunctionArgRule::UsizeSpan;
        }
        if(isMut && tname == "FixedVector" && type.templateArgs.size() == 2u && type.templateArgs[0].type && type.templateArgs[0].type->name == BuiltinTypeSemantics::ByteScalarTypeName())
            return BuiltinFunctionArgRule::MutByteFixedVector;
        if(isMut && tname == "Array" && type.templateArgs.size() == 2u && type.templateArgs[0].type && type.templateArgs[0].type->name == BuiltinTypeSemantics::UInt64ScalarTypeName() && type.templateArgs[1].constantOrTypeName() == "64")
            return BuiltinFunctionArgRule::MutableU64Array64;

        MString key = buildPairKey(tname, isMut ? MStringView("1") : MStringView("0"));
        if(const auto it = m_exactRuleByKey.find(MStringView(key.data(), key.size())); it != m_exactRuleByKey.end())
            return it->second;

        const BuiltinFunctionArgRule::Enum ruleId = allocRuleId();
        const MStringView ownedType = ownString(tname);
        const MStringView msgPrefix = isMut
            ? MStringView("built-in function argument must be a mutable ")
            : MStringView("built-in function argument must be ");
        const MStringView msgSuffix = isMut ? MStringView(" variable") : MStringView{};
        MString msg(m_arena);
        msg.reserve(msgPrefix.size() + ownedType.size() + msgSuffix.size());
        msg.append(msgPrefix.data(), msgPrefix.size());
        msg.append(ownedType.data(), ownedType.size());
        msg.append(msgSuffix.data(), msgSuffix.size());
        const MStringView ownedMsg = allocOwnedString(Move(msg));

        m_argRules.push_back(BuiltinFunctionArgRuleInfo{ ruleId, ownedType, ownedType, ownedMsg, isMut, false, false });
        m_exactRuleByKey.emplace(Move(key), ruleId);
        return ruleId;
    }

    void unpackReturnType(
        const TypeRef* const retType,
        const MStringView templateParamName,
        MStringView& outTypeName,
        MStringView& outTypeArgName,
        MStringView& outNestedTypeArgName,
        MStringView& outErrorTypeName,
        BuiltinFunctionReturnRule::Enum* const outReturnRule
    ){
        outTypeName = {};
        outTypeArgName = {};
        outNestedTypeArgName = {};
        outErrorTypeName = {};
        if(outReturnRule)
            *outReturnRule = BuiltinFunctionReturnRule::FixedType;
        if(!retType)
            return;

        if(!templateParamName.empty()){
            if(retType->name == templateParamName){
                if(outReturnRule)
                    *outReturnRule = BuiltinFunctionReturnRule::CallTemplateArg0;
                return;
            }
            if(!retType->templateArgs.empty() && retType->templateArgs[0].type){
                const TypeRef* const arg0 = retType->templateArgs[0].type;
                if(arg0->name == templateParamName){
                    outTypeName = ownString(retType->name);
                    if(retType->templateArgs.size() >= 2u && retType->templateArgs[1].type)
                        outErrorTypeName = ownString(retType->templateArgs[1].type->name);
                    if(outReturnRule)
                        *outReturnRule = BuiltinFunctionReturnRule::CallTemplateArg0;
                    return;
                }
                if(!arg0->templateArgs.empty() && arg0->templateArgs[0].type && arg0->templateArgs[0].type->name == templateParamName){
                    outTypeName = ownString(retType->name);
                    outTypeArgName = ownString(arg0->name);
                    if(retType->templateArgs.size() >= 2u && retType->templateArgs[1].type)
                        outErrorTypeName = ownString(retType->templateArgs[1].type->name);
                    if(outReturnRule)
                        *outReturnRule = BuiltinFunctionReturnRule::CallTemplateArg0;
                    return;
                }
            }
        }

        outTypeName = ownString(retType->name);
        if(!retType->templateArgs.empty() && retType->templateArgs[0].type){
            const TypeRef* const arg0 = retType->templateArgs[0].type;
            outTypeArgName = ownString(arg0->name);
            if(!arg0->templateArgs.empty() && arg0->templateArgs[0].type)
                outNestedTypeArgName = ownString(arg0->templateArgs[0].type->name);
        }
        if(retType->templateArgs.size() >= 2u && retType->templateArgs[1].type)
            outErrorTypeName = ownString(retType->templateArgs[1].type->name);
    }

    void loadEnums(const MVector<EnumDecl*>& enums){
        for(const EnumDecl* en : enums){
            if(!en || en->name.empty())
                continue;
            BuiltinEnumInfo info{};
            info.adhdName = ownString(en->name);
            info.cppTypeName = ownString(en->cppExportName);
            info.cppScopeName = ownString(ContractSemantics::StripWrappedNwbEnumSuffix(en->cppExportName));
            info.requiredHeader = ownString(en->requiredHeader);
            info.byteSize = static_cast<u32>(en->underlyingSize);
            info.byteAlign = static_cast<u32>(en->underlyingAlign);
            info.isFlag = en->isFlag;

            const usize idx = m_enums.size();
            m_enums.push_back(info);
            insertUniqueIndex(m_enumByName, info.adhdName, idx);

            for(const EnumEntry& entry : en->entries){
                if(entry.name.empty())
                    continue;
                const MStringView ownedEntry = ownString(entry.name);
                m_enumEntries.push_back(BuiltinEnumEntryInfo{ info.adhdName, ownedEntry });
                MString entryKey = buildPairKey(info.adhdName, ownedEntry);
                const MStringView entryKeyView(entryKey.data(), entryKey.size());
                if(m_enumEntryByKey.find(entryKeyView) == m_enumEntryByKey.end())
                    m_enumEntryByKey.emplace(Move(entryKey), 1u);
            }
        }
    }

    void loadClasses(const MVector<ClassDecl*>& classes){
        for(const ClassDecl* cls : classes){
            if(!cls || cls->name.empty())
                continue;
            BuiltinObjectTypeInfo obj{};
            obj.adhdName = ownString(cls->name);
            obj.cppExportName = ownString(cls->cppExportName);
            obj.requiredHeader = ownString(cls->requiredHeader);
            obj.byteSize = cls->hasExplicitLayout ? static_cast<u32>(cls->totalSize) : 8u;
            obj.byteAlign = cls->hasExplicitLayout ? static_cast<u32>(cls->totalAlign) : 8u;
            obj.isArenaBacked = cls->isArenaOwner;
            obj.isSelfDefaulted = cls->isSelfDefaulted;
            obj.isSmallValue = cls->isSmallValue;
            obj.isBorrowView = cls->isBorrowView;
            obj.isBinarySerializable = cls->isBinarySerializable;
            obj.isSingleValueCtor = false;
            obj.emitsBraceFieldInit = cls->emitsBraceFieldInit;
            obj.isStringLike = false;
            obj.isStringSwitch = cls->isEqualitySwitch;
            obj.isNonThreadSafeContainer = cls->isNonThreadSafeContainer;
            obj.isNwbMapKey = cls->isNwbMapKey;
            obj.requiresTCharAdaptation = false;
            obj.arenaCtorMode = cls->arenaCtorMode;
            obj.isCopyMoveConstructible = cls->isCopyConstructible;
            obj.isAssignable = cls->isCopyAssignable;
            obj.isAbstract = cls->isAbstract;
            obj.isPolymorphicBase = cls->isPolymorphicBase;
            obj.formatArgWrapKind = 0u;
            obj.templateParams = cls->templateParams;

            const usize objIdx = m_objectTypes.size();
            m_objectTypes.push_back(obj);
            insertUniqueIndex(m_objectTypeByName, obj.adhdName, objIdx);

            for(const FieldDecl* field : cls->fields){
                if(!field || !field->type || field->name.empty())
                    continue;
                BuiltinFieldInfo fInfo{};
                fInfo.receiverTypeName = obj.adhdName;
                fInfo.fieldName = ownString(field->name);
                fInfo.fieldTypeName = ownString(field->type->name);
                if(!field->type->templateArgs.empty()){
                    fInfo.elemTypeName = ownString(field->type->templateArgs[0].constantOrTypeName());
                    if(field->type->templateArgs.size() >= 2u)
                        fInfo.countText = ownString(field->type->templateArgs[1].constantOrTypeName());
                }
                if(SymbolTable::IsSimdRegisterType(fInfo.fieldTypeName) && m_simdFieldTypes.find(obj.adhdName) == m_simdFieldTypes.end())
                    m_simdFieldTypes.emplace(MString(obj.adhdName.data(), obj.adhdName.size(), m_arena), 1u);

                const usize fIdx = m_fields.size();
                m_fields.push_back(fInfo);
                insertUniqueKey(m_fieldByKey, buildPairKey(obj.adhdName, fInfo.fieldName), fIdx);
            }

            for(const MethodDecl* method : cls->methods){
                if(!method || method->name.empty())
                    continue;
                BuiltinReceiverMethodInfo rm{};
                rm.receiverTypeName = obj.adhdName;
                rm.methodName = ownString(method->name);
                const MStringView methodTplName = !method->templateParams.empty() ? method->templateParams[0].name : MStringView{};
                unpackReturnType(method->returnType, methodTplName, rm.returnTypeName, rm.returnTypeArgName, rm.returnNestedTypeArgName, rm.returnErrorTypeName, nullptr);
                if(method->returnType && !method->returnType->templateArgs.empty() && method->returnType->templateArgs[0].type)
                    rm.returnTypeArgRefMode = method->returnType->templateArgs[0].type->refMode;
                rm.synthesizeTypeRef = true;
                rm.isMutating = !method->isConst;
                rm.returnsReference = method->returnsReference || (method->returnType && method->returnType->refMode != ParamPassingMode::None);

                const usize rmIdx = m_receiverMethods.size();
                m_receiverMethods.push_back(rm);
                insertUniqueKey(m_receiverMethodByKey, buildPairKey(obj.adhdName, rm.methodName), rmIdx);

                if(!method->isVariadic && !method->isOperator && method->params.size() <= 9u){
                    BuiltinReceiverSignatureInfo sig{};
                    sig.receiverTypeName = obj.adhdName;
                    sig.methodName = rm.methodName;
                    for(BuiltinFunctionArgRule::Enum& r : sig.argRules)
                        r = BuiltinFunctionArgRule::Any;
                    sig.arity = static_cast<u8>(method->params.size());
                    sig.allowsCppUnwind = method->allowsCppUnwind;
                    for(usize pi = 0u; pi < method->params.size(); ++pi){
                        sig.argRules[pi] = resolveArgRule(method->params[pi]);
                        if(method->params[pi].cppConstReference)
                            sig.cppConstReferenceMask |= 1u << pi;
                        if(method->params[pi].retainsBorrow)
                            sig.retainedArgMask |= 1u << pi;
                    }

                    const usize sigIdx = m_receiverSignatures.size();
                    m_receiverSignatures.push_back(sig);
                    insertUniqueKey(m_receiverSigByKey, buildPairKey(obj.adhdName, rm.methodName), sigIdx);
                }
            }
        }

        const auto computeTypeLayout = [&](const auto& self, const TypeRef& type, const u32 depth, usize& outSize, usize& outAlign)noexcept -> void{
            if(depth > 16u || type.refMode != ParamPassingMode::None || type.isFuncType){
                outSize = 8u;
                outAlign = 8u;
                return;
            }
            const MStringView tname = type.name;
            if(const BuiltinScalarTypeInfo* scalarInfo = BuiltinTypeSemantics::FindBuiltinScalarType(tname)){
                outSize = scalarInfo->byteSize;
                outAlign = scalarInfo->byteAlign;
                return;
            }
            if(const BuiltinObjectTypeInfo* objInfo = FindBuiltinByAdhdName(s_BuiltinObjectTypes, tname)){
                outSize = objInfo->byteSize;
                outAlign = objInfo->byteAlign;
                return;
            }
            if(const BuiltinObjectTypeInfo* objInfo = findObjectType(tname)){
                outSize = objInfo->byteSize;
                outAlign = objInfo->byteAlign;
                return;
            }
            if(const BuiltinEnumInfo* enumInfo = findEnum(tname)){
                outSize = enumInfo->byteSize;
                outAlign = enumInfo->byteAlign;
                return;
            }
            if(const BuiltinUnalignedVectorInfo* unalignedInfo = FindBuiltinByAdhdName(s_BuiltinUnalignedVectorTypes, tname)){
                outSize = unalignedInfo->byteSize;
                outAlign = unalignedInfo->byteAlign;
                return;
            }
            if(const BuiltinSimdVectorInfo* vecInfo = FindBuiltinByAdhdName(s_BuiltinSimdVectorTypes, tname)){
                outSize = vecInfo->byteSize;
                outAlign = vecInfo->byteAlign;
                return;
            }
            if(const BuiltinSimdMatrixInfo* matInfo = FindBuiltinByAdhdName(s_BuiltinSimdMatrixTypes, tname)){
                outSize = matInfo->byteSize;
                outAlign = matInfo->byteAlign;
                return;
            }
            if(const BuiltinGenericTypeInfo* genericInfo = FindBuiltinByAdhdName(s_BuiltinGenericTypes, tname)){
                if(genericInfo->byteSize > 0u){
                    outSize = genericInfo->byteSize;
                    outAlign = genericInfo->byteAlign;
                    return;
                }
                const auto resolveInner = [&](const TypeRef& innerType, const u32 innerDepth, usize& innerSize, usize& innerAlign)noexcept{
                    self(self, innerType, innerDepth, innerSize, innerAlign);
                };
                const auto parseCount = [](const MStringView countText)noexcept -> usize{
                    u64 countVal = 0u;
                    if(LiteralDecoder::TryParseUnsignedIntegerLiteral(countText, countVal) && countVal > 0u)
                        return static_cast<usize>(countVal);
                    return 1u;
                };
                if(ResolveInlineGenericLayout(*genericInfo, type, depth, outSize, outAlign, resolveInner, parseCount))
                    return;
            }
            outSize = 8u;
            outAlign = 8u;
        };

        for(usize pass = 0u; pass <= classes.size(); ++pass){
            b8 changed = false;
            for(const ClassDecl* cls : classes){
                if(!cls || cls->name.empty() || cls->hasExplicitLayout)
                    continue;
                const auto it = m_objectTypeByName.find(cls->name);
                if(it == m_objectTypeByName.end())
                    continue;
                usize computedSize = 8u;
                usize computedAlign = 8u;
                const auto resolveField = [&](const TypeRef& fieldType, usize& fieldSize, usize& fieldAlign)noexcept{
                    computeTypeLayout(computeTypeLayout, fieldType, 0u, fieldSize, fieldAlign);
                };
                ComputeSequentialExternClassLayout(*cls, cls->isPolymorphicBase, resolveField, computedSize, computedAlign);
                const u32 newSize = computedSize <= Limit<u32>::s_Max ? static_cast<u32>(computedSize) : Limit<u32>::s_Max;
                const u32 newAlign = computedAlign <= Limit<u32>::s_Max ? static_cast<u32>(computedAlign) : Limit<u32>::s_Max;
                BuiltinObjectTypeInfo& obj = m_objectTypes[it->second];
                if(obj.byteSize != newSize || obj.byteAlign != newAlign){
                    obj.byteSize = newSize;
                    obj.byteAlign = newAlign;
                    changed = true;
                }
            }
            if(!changed)
                break;
        }
    }

    void loadFunctions(const MVector<MethodDecl*>& functions){
        for(const MethodDecl* fn : functions){
            if(!fn || fn->name.empty())
                continue;
            BuiltinFunctionInfo info{};
            info.adhdName = ownString(fn->name);
            info.cppExportName = ownString(fn->cppExportName);
            info.requiredHeader = ownString(fn->requiredHeader);
            const MStringView fnTplName = !fn->templateParams.empty() ? fn->templateParams[0].name : MStringView{};
            unpackReturnType(fn->returnType, fnTplName, info.returnTypeName, info.returnTypeArgName, info.returnNestedTypeArgName, info.returnErrorTypeName, &info.returnRule);
            info.minArity = fn->minArgCount;
            info.arity = fn->params.size() <= info.argRules.size() ? fn->params.size() : info.argRules.size();
            if(fn->callLoweringKind == static_cast<u8>(ExternCallableFlagKind::PlatformLiteralArg0))
                info.callLowering = BuiltinCallLowering::PlatformLiteralArg0;
            else if(fn->callLoweringKind == static_cast<u8>(ExternCallableFlagKind::ByteViewArg0))
                info.callLowering = BuiltinCallLowering::BinaryByteViewArg0;
            else if(fn->callLoweringKind == static_cast<u8>(ExternCallableFlagKind::ByteViewArg1))
                info.callLowering = BuiltinCallLowering::BinaryByteViewArg1;
            else
                info.callLowering = BuiltinCallLowering::None;
            info.prependsScratchArena = fn->injectsCallerArena;
            info.isVariadic = fn->isVariadic;
            info.wrapsFormatArgs = false;
            info.requiresExplicitTypeArg = !fn->templateParams.empty();
            info.returnsBorrowedView = fn->returnsBorrowedView;
            info.rejectsSimdStorage = false;
            info.returnsReference = fn->returnsReference || (fn->returnType && fn->returnType->refMode != ParamPassingMode::None);
            for(BuiltinFunctionArgRule::Enum& r : info.argRules)
                r = BuiltinFunctionArgRule::Any;
            for(usize pi = 0u; pi < info.arity; ++pi){
                const ParamDecl& param = fn->params[pi];
                if(!fnTplName.empty() && param.type && param.type->name == fnTplName && param.type->templateArgs.empty()){
                    info.explicitTypeParamMask |= 1u << pi;
                    info.argRules[pi] = param.mode == ParamPassingMode::Mut ? BuiltinFunctionArgRule::MutAny : BuiltinFunctionArgRule::Any;
                }
                else
                    info.argRules[pi] = resolveArgRule(param);
                if(fn->params[pi].retainsBorrow)
                    info.retainedArgMask |= 1u << pi;
            }

            const usize fnIdx = m_functions.size();
            m_functions.push_back(info);
            insertUniqueIndex(m_functionByName, info.adhdName, fnIdx);
            insertUniqueIndex(m_functionByCppName, info.cppExportName, fnIdx);
        }
    }

    void loadGlobalConstants(const MVector<Stmt*>& constants){
        for(const Stmt* stmt : constants){
            if(!stmt || stmt->kind != StmtKind::VarDecl || stmt->name.empty() || !stmt->varType)
                continue;
            BuiltinGlobalConstantInfo gc{};
            gc.adhdName = ownString(stmt->name);
            gc.cppExportName = ownString(stmt->cppExportName);
            gc.adhdTypeName = ownString(stmt->varType->name);
            gc.requiredHeader = ownString(stmt->requiredHeader);

            const usize gcIdx = m_globalConstants.size();
            m_globalConstants.push_back(gc);
            insertUniqueIndex(m_globalConstantByName, gc.adhdName, gcIdx);
            insertUniqueIndex(m_globalConstantByCppName, gc.cppExportName, gcIdx);
        }
    }

    void recordLoadError(const MStringView source, const Span<const ParseError> errors, const MStringView fallback){
        m_loadError.assign("ADHD extern prelude");
        if(!source.empty())
            m_loadError.append(" '").append(source).append("'");
        if(errors.empty()){
            m_loadError.append(": ").append(fallback);
            return;
        }
        const ParseError& first = errors.front();
        m_loadError.append(StringFormat(m_arena, ":{}:{}: {}", first.line, first.column, first.message));
    }

    void load(){
        Alloc::ScratchArena scratchArena(MetascriptArenaScope::s_ParserScratch);
        MetaArena parseArena(MetascriptArenaScope::s_ParserScratch);

        MString rawGlobalSource(parseArena);
        const b8 loadedGlobal = tryReadProjectFile(ContractSemantics::s_GlobalExternPreludePath, parseArena, rawGlobalSource);
        MString rawCoreSource(parseArena);
        const b8 loadedCore = tryReadProjectFile(ContractSemantics::s_CoreExternPreludePath, parseArena, rawCoreSource);
        if(!loadedGlobal || rawGlobalSource.empty() || !loadedCore || rawCoreSource.empty()){
            const MStringView source = (!loadedGlobal || rawGlobalSource.empty()) ? ContractSemantics::s_GlobalExternPreludePath : ContractSemantics::s_CoreExternPreludePath;
            recordLoadError(source, {}, "file is missing or empty");
            return;
        }

        MString rawPreludeSource(parseArena);
        rawPreludeSource.reserve(rawGlobalSource.size() + 2u + rawCoreSource.size());
        rawPreludeSource.append(rawGlobalSource.data(), rawGlobalSource.size());
        rawPreludeSource.append("\r\n", 2u);
        rawPreludeSource.append(rawCoreSource.data(), rawCoreSource.size());

        MVector<ParseError> errors(parseArena);
        PreludeIncludeResolver includeResolver(*this);
        Preprocessor preprocessor(parseArena, scratchArena, errors, &includeResolver, false);
        m_sourceText.clear();
        const b8 preprocessed = preprocessor.process(MStringView(rawPreludeSource.data(), rawPreludeSource.size()), m_sourceText);
        if(!preprocessed || !errors.empty() || m_sourceText.empty()){
            recordLoadError({}, errors, "preprocessing yielded no declarations");
            return;
        }

        const MStringView activeSource(m_sourceText.data(), m_sourceText.size());
        TranslationUnit parseUnit(parseArena);
        ParserCursor cursor(activeSource, m_arena, scratchArena, errors);
        DeclParser declParser(cursor, activeSource, nullptr);
        const b8 parsed = declParser.parseTranslationUnit(parseUnit);
        if(!parsed || !errors.empty()){
            const MStringView token = cursor.current().text;
            MStringView source;
            if(!token.empty() && token.data() >= activeSource.data() && token.data() < activeSource.data() + activeSource.size())
                source = preprocessor.includedFileAtOffset(static_cast<usize>(token.data() - activeSource.data()));
            recordLoadError(source, errors, "unable to parse declarations");
            return;
        }

        loadEnums(parseUnit.enums);
        loadClasses(parseUnit.classes);
        loadFunctions(parseUnit.freeFunctions);
        loadGlobalConstants(parseUnit.globalConstants);
    }


private:
    MetaArena m_arena;
    MString m_sourceText;
    MString m_loadError;
    MVector<GlobalUniquePtr<MString>> m_ownedStrings;
    MVector<BuiltinObjectTypeInfo> m_objectTypes;
    MVector<BuiltinEnumInfo> m_enums;
    MVector<BuiltinEnumEntryInfo> m_enumEntries;
    MVector<BuiltinFieldInfo> m_fields;
    MVector<BuiltinReceiverMethodInfo> m_receiverMethods;
    MVector<BuiltinReceiverSignatureInfo> m_receiverSignatures;
    MVector<BuiltinFunctionArgRuleInfo> m_argRules;
    MVector<BuiltinVisitorRuleInfo> m_visitorRules;
    MVector<BuiltinFunctionInfo> m_functions;
    MVector<BuiltinGlobalConstantInfo> m_globalConstants;
    MStringMap<usize> m_objectTypeByName;
    MStringMap<usize> m_enumByName;
    MStringMap<u8> m_enumEntryByKey;
    MStringMap<usize> m_fieldByKey;
    MStringMap<usize> m_receiverMethodByKey;
    MStringMap<usize> m_receiverSigByKey;
    MStringMap<usize> m_functionByName;
    MStringMap<usize> m_functionByCppName;
    MStringMap<usize> m_globalConstantByName;
    MStringMap<usize> m_globalConstantByCppName;
    MStringMap<u8> m_simdFieldTypes;
    MStringMap<BuiltinFunctionArgRule::Enum> m_exactRuleByKey;
    MStringMap<BuiltinFunctionArgRule::Enum> m_visitorRuleByKey;
};

[[nodiscard]] inline const CoreExternRegistry& GetCoreExternRegistry()noexcept{
    static const CoreExternRegistry s_Registry;
    return s_Registry;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


MStringView BuiltinPreludeLoadError()noexcept{
    return __hidden_adhd_type_utils::GetCoreExternRegistry().loadError();
}

const BuiltinObjectTypeInfo* FindBuiltinObjectType(const MStringView name)noexcept{
    if(const BuiltinObjectTypeInfo* info = __hidden_adhd_type_utils::FindBuiltinByAdhdName(__hidden_adhd_type_utils::s_BuiltinObjectTypes, name))
        return info;
    return __hidden_adhd_type_utils::GetCoreExternRegistry().findObjectType(name);
}

const BuiltinEnumInfo* FindBuiltinEnum(const MStringView name)noexcept{
    return __hidden_adhd_type_utils::GetCoreExternRegistry().findEnum(name);
}

b8 HasBuiltinEnumEntry(const MStringView enumName, const MStringView entryName)noexcept{
    return __hidden_adhd_type_utils::GetCoreExternRegistry().hasEnumEntry(enumName, entryName);
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
    return __hidden_adhd_type_utils::GetCoreExternRegistry().findReceiverMethod(receiverTypeName, methodName);
}

const BuiltinReceiverSignatureInfo* FindBuiltinReceiverSignature(const MStringView receiverTypeName, const MStringView methodName)noexcept{
    return __hidden_adhd_type_utils::GetCoreExternRegistry().findReceiverSignature(receiverTypeName, methodName);
}

const BuiltinFieldInfo* FindBuiltinField(const MStringView receiverTypeName, const MStringView fieldName)noexcept{
    for(const BuiltinFieldInfo& info : __hidden_adhd_type_utils::s_BuiltinFields){
        if(info.receiverTypeName == receiverTypeName && info.fieldName == fieldName)
            return &info;
    }
    return __hidden_adhd_type_utils::GetCoreExternRegistry().findField(receiverTypeName, fieldName);
}

const BuiltinFunctionArgRuleInfo* FindBuiltinFunctionArgRule(const BuiltinFunctionArgRule::Enum rule)noexcept{
    if(const BuiltinFunctionArgRuleInfo* info = __hidden_adhd_type_utils::FindBuiltinByRule(__hidden_adhd_type_utils::s_BuiltinFunctionArgRules, rule))
        return info;
    return __hidden_adhd_type_utils::GetCoreExternRegistry().findArgRule(rule);
}

const BuiltinVisitorRuleInfo* FindBuiltinVisitorRule(const BuiltinFunctionArgRule::Enum rule)noexcept{
    return __hidden_adhd_type_utils::GetCoreExternRegistry().findVisitorRule(rule);
}

const BuiltinFunctionInfo* FindBuiltinFunction(const MStringView name)noexcept{
    if(const BuiltinFunctionInfo* info = __hidden_adhd_type_utils::FindBuiltinByAdhdName(__hidden_adhd_type_utils::s_BuiltinFunctions, name))
        return info;
    return __hidden_adhd_type_utils::GetCoreExternRegistry().findFunction(name);
}

const BuiltinFunctionInfo* FindBuiltinScopedFunction(const MStringView scopeTypeName, const MStringView memberName)noexcept{
    if(const BuiltinFunctionInfo* info = __hidden_adhd_type_utils::FindBuiltinScopedSymbol(__hidden_adhd_type_utils::s_BuiltinFunctions, scopeTypeName, memberName, true))
        return info;
    return __hidden_adhd_type_utils::GetCoreExternRegistry().findScopedFunction(scopeTypeName, memberName);
}

const BuiltinGlobalConstantInfo* FindBuiltinGlobalConstant(const MStringView name)noexcept{
    return __hidden_adhd_type_utils::GetCoreExternRegistry().findGlobalConstant(name);
}

const BuiltinGlobalConstantInfo* FindBuiltinScopedConstant(const MStringView scopeTypeName, const MStringView memberName)noexcept{
    return __hidden_adhd_type_utils::GetCoreExternRegistry().findScopedConstant(scopeTypeName, memberName);
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
    if(FindSpecialMethodSemantics(methodName) == SpecialMethodKind::CheckedElementAccess)
        return genericInfo->isReflectedMap || genericInfo->isIndexableSequence;
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
    return __hidden_adhd_type_utils::GetCoreExternRegistry().hasSimdRegisterField(typeName);
}

b8 IsOverAlignedBuiltinType(const MStringView typeName)noexcept{
    usize byteSize = 0u;
    usize byteAlign = 0u;
    return FindBuiltinFixedLayout(typeName, byteSize, byteAlign) && byteAlign >= 16u;
}

b8 IsStringBorrowViewType(const MStringView name)noexcept{
    return IsStringLikeType(name) && IsBorrowViewType(name);
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

