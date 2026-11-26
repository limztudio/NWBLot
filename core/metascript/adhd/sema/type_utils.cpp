// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/metascript/adhd/ast/builtin_type_semantics.h>
#include <core/metascript/adhd/ast/operator_semantics.h>
#include <core/metascript/adhd/parser/decl_parser.h>
#include <core/metascript/arena_names.h>
#include <core/metascript/literal_decoder.h>

#include <global/atomic.h>
#include <global/environment.h>
#include <global/filesystem.h>
#include <global/hash_utils.h>

#if !defined(FOUNDATION_PLATFORM_WINDOWS)
#include <unistd.h>
#endif

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
#define NWB_ADHD_EXPORT_RECEIVER_METHOD_MUT(receiverTypeName, methodName, returnTypeName, returnTypeArgName, returnErrorTypeName, synthesizeTypeRef, isMutating) \
    BuiltinReceiverMethodInfo{ receiverTypeName, methodName, returnTypeName, returnTypeArgName, "", returnErrorTypeName, synthesizeTypeRef, isMutating, false },
#include "../export_functions.h"
#undef NWB_ADHD_EXPORT_RECEIVER_METHOD_MUT
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

[[nodiscard]] consteval b8 AreStaticArgRulesDense()noexcept{
    for(usize i = 0u; i < static_cast<usize>(BuiltinFunctionArgRule::StaticCount); ++i){
        if(static_cast<usize>(s_BuiltinFunctionArgRules[i].rule) != i)
            return false;
    }
    return true;
}

static_assert(AreStaticArgRulesDense());

inline constexpr BuiltinFunctionInfo s_BuiltinFunctions[] = {
#define NWB_ADHD_EXPORT_FUNCTION(adhdName, cppExportName, arity, returnRule, returnTypeName, returnTypeArgName, arg0Rule, arg1Rule, arg2Rule) \
    BuiltinFunctionInfo{ adhdName, cppExportName, returnTypeName, returnTypeArgName, "", "", "", arity, arity, BuiltinFunctionReturnRule::returnRule, BuiltinCallLowering::None, false, false, false, false, false, false, false, { BuiltinFunctionArgRule::arg0Rule, BuiltinFunctionArgRule::arg1Rule, BuiltinFunctionArgRule::arg2Rule, BuiltinFunctionArgRule::Any, BuiltinFunctionArgRule::Any, BuiltinFunctionArgRule::Any, BuiltinFunctionArgRule::Any, BuiltinFunctionArgRule::Any, BuiltinFunctionArgRule::Any } },
#define NWB_ADHD_EXPORT_FUNCTION_EX(adhdName, cppExportName, minArity, arity, returnRule, returnTypeName, returnTypeArgName, returnNestedTypeArgName, returnErrorTypeName, requiredHeader, prependsScratchArena, isVariadic, wrapsFormatArgs, arg0Rule, arg1Rule, arg2Rule, arg3Rule, arg4Rule) \
    BuiltinFunctionInfo{ adhdName, cppExportName, returnTypeName, returnTypeArgName, returnNestedTypeArgName, returnErrorTypeName, requiredHeader, minArity, arity, BuiltinFunctionReturnRule::returnRule, BuiltinCallLowering::None, prependsScratchArena, isVariadic, wrapsFormatArgs, (BuiltinFunctionReturnRule::returnRule == BuiltinFunctionReturnRule::CallTemplateArg0), false, false, false, { BuiltinFunctionArgRule::arg0Rule, BuiltinFunctionArgRule::arg1Rule, BuiltinFunctionArgRule::arg2Rule, BuiltinFunctionArgRule::arg3Rule, BuiltinFunctionArgRule::arg4Rule, BuiltinFunctionArgRule::Any, BuiltinFunctionArgRule::Any, BuiltinFunctionArgRule::Any, BuiltinFunctionArgRule::Any } },
#define NWB_ADHD_EXPORT_FUNCTION_CALL(adhdName, cppExportName, minArity, arity, returnRule, returnTypeName, returnTypeArgName, returnNestedTypeArgName, returnErrorTypeName, requiredHeader, prependsScratchArena, isVariadic, wrapsFormatArgs, callLowering, requiresExplicitTypeArg, returnsBorrowedView, rejectsSimdStorage, arg0Rule, arg1Rule, arg2Rule, arg3Rule, arg4Rule, arg5Rule, arg6Rule, arg7Rule, arg8Rule) \
    BuiltinFunctionInfo{ adhdName, cppExportName, returnTypeName, returnTypeArgName, returnNestedTypeArgName, returnErrorTypeName, requiredHeader, minArity, arity, BuiltinFunctionReturnRule::returnRule, BuiltinCallLowering::callLowering, prependsScratchArena, isVariadic, wrapsFormatArgs, requiresExplicitTypeArg, returnsBorrowedView, rejectsSimdStorage, false, { BuiltinFunctionArgRule::arg0Rule, BuiltinFunctionArgRule::arg1Rule, BuiltinFunctionArgRule::arg2Rule, BuiltinFunctionArgRule::arg3Rule, BuiltinFunctionArgRule::arg4Rule, BuiltinFunctionArgRule::arg5Rule, BuiltinFunctionArgRule::arg6Rule, BuiltinFunctionArgRule::arg7Rule, BuiltinFunctionArgRule::arg8Rule } },
#include "../export_functions.h"
#undef NWB_ADHD_EXPORT_FUNCTION_CALL
#undef NWB_ADHD_EXPORT_FUNCTION_EX
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

struct StringViewPair{
    MStringView first;
    MStringView second;

    [[nodiscard]] b8 operator==(const StringViewPair& rhs)const noexcept{
        return first == rhs.first && second == rhs.second;
    }
};

struct StringViewPairHash{
    [[nodiscard]] usize operator()(const StringViewPair& key)const noexcept{
        const u64 h0 = ComputeFnv64Bytes(key.first.data(), key.first.size());
        const u64 h1 = ComputeFnv64Bytes(key.second.data(), key.second.size());
        return static_cast<usize>(MixMurmur64Finalizer(h0 ^ MixMurmur64Finalizer(h1)));
    }
};

template<typename MapT, typename ValueT>
void IndexScopedSymbolKey(MapT& map, const MStringView cppExportName, const ValueT value, const b8 allowArenaTemplateScope){
    constexpr MStringView s_ScopeSep = ContractSemantics::s_CppScopeSeparator;
    constexpr MStringView s_ArenaSuffix = ContractSemantics::s_CppArenaTemplateScopeSep;
    if(const usize sepPos = cppExportName.rfind(s_ScopeSep); sepPos != MStringView::npos){
        const MStringView prefix = cppExportName.substr(0u, sepPos);
        const MStringView member = cppExportName.substr(sepPos + s_ScopeSep.size());
        if(!prefix.empty() && !member.empty())
            map.emplace(StringViewPair{ prefix, member }, value);
    }
    if(allowArenaTemplateScope){
        if(const usize arenaSepPos = cppExportName.rfind(s_ArenaSuffix); arenaSepPos != MStringView::npos){
            const MStringView prefix = cppExportName.substr(0u, arenaSepPos);
            const MStringView member = cppExportName.substr(arenaSepPos + s_ArenaSuffix.size());
            if(!prefix.empty() && !member.empty())
                map.emplace(StringViewPair{ prefix, member }, value);
        }
    }
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

class StaticBuiltinTableIndex final{
public:
    StaticBuiltinTableIndex()
        : m_arena(MetascriptArenaScope::s_ParserScratch)
        , m_objectTypeByName(m_arena)
        , m_genericTypeByName(m_arena)
        , m_unalignedVectorByName(m_arena)
        , m_simdVectorByName(m_arena)
        , m_simdMatrixByName(m_arena)
        , m_memberMethodByName(m_arena)
        , m_receiverMethodByKey(m_arena)
        , m_fieldByKey(m_arena)
        , m_functionByName(m_arena)
        , m_scopedFunctionByKey(m_arena)
        , m_specialFunctionByName(m_arena)
        , m_simdLaneZeroRuleByName(m_arena)
        , m_specialMethodByName(m_arena)
        , m_simdRegisterFieldReceivers(m_arena)
    {
        for(const BuiltinObjectTypeInfo& info : s_BuiltinObjectTypes)
            m_objectTypeByName.emplace(info.adhdName, &info);
        for(const BuiltinGenericTypeInfo& info : s_BuiltinGenericTypes)
            m_genericTypeByName.emplace(info.adhdName, &info);
        for(const BuiltinUnalignedVectorInfo& info : s_BuiltinUnalignedVectorTypes)
            m_unalignedVectorByName.emplace(info.adhdName, &info);
        for(const BuiltinSimdVectorInfo& info : s_BuiltinSimdVectorTypes)
            m_simdVectorByName.emplace(info.adhdName, &info);
        for(const BuiltinSimdMatrixInfo& info : s_BuiltinSimdMatrixTypes)
            m_simdMatrixByName.emplace(info.adhdName, &info);
        for(const BuiltinMemberMethodInfo& info : s_BuiltinMemberMethods)
            m_memberMethodByName.emplace(info.methodName, &info);
        for(const BuiltinReceiverMethodInfo& info : s_BuiltinReceiverMethods)
            m_receiverMethodByKey.emplace(StringViewPair{ info.receiverTypeName, info.methodName }, &info);
        for(const BuiltinFieldInfo& info : s_BuiltinFields){
            m_fieldByKey.emplace(StringViewPair{ info.receiverTypeName, info.fieldName }, &info);
            const auto vIt = m_simdVectorByName.find(info.fieldTypeName);
            const auto mIt = m_simdMatrixByName.find(info.fieldTypeName);
            const b8 isSimdReg = (vIt != m_simdVectorByName.end() && vIt->second->cppExportName == vIt->second->regCppType)
                || (mIt != m_simdMatrixByName.end() && mIt->second->cppExportName == mIt->second->regCppType);
            if(isSimdReg)
                m_simdRegisterFieldReceivers.insert(info.receiverTypeName);
        }
        for(const BuiltinFunctionInfo& info : s_BuiltinFunctions){
            m_functionByName.emplace(info.adhdName, &info);
            IndexScopedSymbolKey(m_scopedFunctionByKey, info.cppExportName, &info, true);
        }
        for(const BuiltinSpecialFunctionInfo& info : s_SpecialFunctions)
            m_specialFunctionByName.emplace(info.adhdName, info.kind);
        for(const BuiltinSimdLaneZeroRuleInfo& info : s_SimdLaneZeroRules)
            m_simdLaneZeroRuleByName.emplace(info.adhdName, info.ruleKind);
        for(const BuiltinSpecialMethodInfo& info : s_SpecialMethods)
            m_specialMethodByName.emplace(info.methodName, info.kind);
    }

    template<typename MapT, typename KeyT>
    [[nodiscard]] static auto FindPtr(const MapT& map, const KeyT& key)noexcept{
        const auto it = map.find(key);
        return (it != map.end()) ? it->second : decltype(it->second){ nullptr };
    }

    [[nodiscard]] const BuiltinObjectTypeInfo* findObjectType(const MStringView name)const noexcept{ return FindPtr(m_objectTypeByName, name); }
    [[nodiscard]] const BuiltinGenericTypeInfo* findGenericType(const MStringView name)const noexcept{ return FindPtr(m_genericTypeByName, name); }
    [[nodiscard]] const BuiltinUnalignedVectorInfo* findUnalignedVectorType(const MStringView name)const noexcept{ return FindPtr(m_unalignedVectorByName, name); }
    [[nodiscard]] const BuiltinSimdVectorInfo* findSimdVectorType(const MStringView name)const noexcept{ return FindPtr(m_simdVectorByName, name); }
    [[nodiscard]] const BuiltinSimdMatrixInfo* findSimdMatrixType(const MStringView name)const noexcept{ return FindPtr(m_simdMatrixByName, name); }
    [[nodiscard]] const BuiltinMemberMethodInfo* findMemberMethod(const MStringView name)const noexcept{ return FindPtr(m_memberMethodByName, name); }
    [[nodiscard]] const BuiltinReceiverMethodInfo* findReceiverMethod(const MStringView receiverTypeName, const MStringView methodName)const noexcept{ return FindPtr(m_receiverMethodByKey, StringViewPair{ receiverTypeName, methodName }); }
    [[nodiscard]] const BuiltinFieldInfo* findField(const MStringView receiverTypeName, const MStringView fieldName)const noexcept{ return FindPtr(m_fieldByKey, StringViewPair{ receiverTypeName, fieldName }); }
    [[nodiscard]] const BuiltinFunctionInfo* findFunction(const MStringView name)const noexcept{ return FindPtr(m_functionByName, name); }
    [[nodiscard]] const BuiltinFunctionInfo* findScopedFunction(const MStringView prefix, const MStringView memberName)const noexcept{ return FindPtr(m_scopedFunctionByKey, StringViewPair{ prefix, memberName }); }
    [[nodiscard]] b8 hasSimdRegisterField(const MStringView typeName)const noexcept{ return m_simdRegisterFieldReceivers.find(typeName) != m_simdRegisterFieldReceivers.end(); }
    [[nodiscard]] SpecialFunctionKind::Enum findSpecialFunctionKind(const MStringView name)const noexcept{
        const auto it = m_specialFunctionByName.find(name);
        return (it != m_specialFunctionByName.end()) ? it->second : SpecialFunctionKind::None;
    }
    [[nodiscard]] SimdLaneZeroRuleKind::Enum findSimdLaneZeroRule(const MStringView name)const noexcept{
        const auto it = m_simdLaneZeroRuleByName.find(name);
        return (it != m_simdLaneZeroRuleByName.end()) ? it->second : SimdLaneZeroRuleKind::None;
    }
    [[nodiscard]] SpecialMethodKind::Enum findSpecialMethodKind(const MStringView name)const noexcept{
        const auto it = m_specialMethodByName.find(name);
        return (it != m_specialMethodByName.end()) ? it->second : SpecialMethodKind::None;
    }


private:
    MetaArena m_arena;
    HashMap<MStringView, const BuiltinObjectTypeInfo*, MetaArena, MStringViewHash> m_objectTypeByName;
    HashMap<MStringView, const BuiltinGenericTypeInfo*, MetaArena, MStringViewHash> m_genericTypeByName;
    HashMap<MStringView, const BuiltinUnalignedVectorInfo*, MetaArena, MStringViewHash> m_unalignedVectorByName;
    HashMap<MStringView, const BuiltinSimdVectorInfo*, MetaArena, MStringViewHash> m_simdVectorByName;
    HashMap<MStringView, const BuiltinSimdMatrixInfo*, MetaArena, MStringViewHash> m_simdMatrixByName;
    HashMap<MStringView, const BuiltinMemberMethodInfo*, MetaArena, MStringViewHash> m_memberMethodByName;
    HashMap<StringViewPair, const BuiltinReceiverMethodInfo*, MetaArena, StringViewPairHash> m_receiverMethodByKey;
    HashMap<StringViewPair, const BuiltinFieldInfo*, MetaArena, StringViewPairHash> m_fieldByKey;
    HashMap<MStringView, const BuiltinFunctionInfo*, MetaArena, MStringViewHash> m_functionByName;
    HashMap<StringViewPair, const BuiltinFunctionInfo*, MetaArena, StringViewPairHash> m_scopedFunctionByKey;
    HashMap<MStringView, SpecialFunctionKind::Enum, MetaArena, MStringViewHash> m_specialFunctionByName;
    HashMap<MStringView, SimdLaneZeroRuleKind::Enum, MetaArena, MStringViewHash> m_simdLaneZeroRuleByName;
    HashMap<MStringView, SpecialMethodKind::Enum, MetaArena, MStringViewHash> m_specialMethodByName;
    HashSet<MStringView, MetaArena, MStringViewHash> m_simdRegisterFieldReceivers;
};

[[nodiscard]] inline const StaticBuiltinTableIndex& GetStaticBuiltinTableIndex()noexcept{
    static const StaticBuiltinTableIndex s_Index;
    return s_Index;
}

class CoreExternRegistry final{
private:
    struct WatchedPreludeFile{
        MString relativePath;
        u64 fileSize = 0u;
        i64 mtimeTicks = 0;
        b8 fromProjectRoot = false;


    public:
        explicit WatchedPreludeFile(MetaArena& arena)noexcept
            : relativePath(arena)
        {}
    };

    struct SerializedStringRef{
        u32 offset = 0u;
        u32 length = 0u;
    };

    static inline constexpr u32 s_BinaryCacheMagic = 0x4E574250u;
    static inline constexpr u32 s_BinaryCacheVersion = 2u;

    [[nodiscard]] static u64 ComputeBinaryCacheSchemaSignature()noexcept{
        u64 sig = s_Fnv64OffsetBasis;
        const auto mix = [&](const u64 val)noexcept{
            Fnv64AppendValue(sig, val);
        };
        mix(s_BinaryCacheMagic);
        mix(s_BinaryCacheVersion);
        mix(sizeof(BuiltinObjectTypeInfo));
        mix(sizeof(BuiltinEnumInfo));
        mix(sizeof(BuiltinEnumEntryInfo));
        mix(sizeof(BuiltinFieldInfo));
        mix(sizeof(BuiltinReceiverMethodInfo));
        mix(sizeof(BuiltinReceiverSignatureInfo));
        mix(sizeof(BuiltinFunctionArgRuleInfo));
        mix(sizeof(BuiltinVisitorRuleInfo));
        mix(sizeof(BuiltinFunctionInfo));
        mix(sizeof(BuiltinGlobalConstantInfo));
        mix(sizeof(ClassDecl));
        mix(sizeof(MethodDecl));
        mix(sizeof(ParamDecl));
        mix(sizeof(TypeRef));
        mix(sizeof(TemplateParamDecl));
        mix(sizeof(usize));
        mix(static_cast<u64>(BuiltinFunctionArgRule::StaticCount));
        sig = UpdateFnv64TextExact(sig, ContractSemantics::s_GlobalExternPreludePath);
        sig = UpdateFnv64TextExact(sig, ContractSemantics::s_CoreExternPreludePath);
#if defined(NWB_PROJECT_SOURCE_DIR)
        sig = UpdateFnv64TextExact(sig, MStringView(NWB_PROJECT_SOURCE_DIR));
#endif
        return sig;
    }

    [[nodiscard]] static b8 QueryDiskFileStat(const ::Path<MetaArena>& diskPath, u64& outSize, i64& outMtimeTicks)noexcept{
#if defined(FOUNDATION_PLATFORM_WINDOWS)
        WIN32_FILE_ATTRIBUTE_DATA attrData{};
        if(!GetFileAttributesExW(diskPath.c_str(), GetFileExInfoStandard, &attrData))
            return false;
        if((attrData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0u)
            return false;
        outSize = (static_cast<u64>(attrData.nFileSizeHigh) << 32u) | static_cast<u64>(attrData.nFileSizeLow);
        outMtimeTicks = static_cast<i64>((static_cast<u64>(attrData.ftLastWriteTime.dwHighDateTime) << 32u) | static_cast<u64>(attrData.ftLastWriteTime.dwLowDateTime));
        return true;
#else
        struct stat status{};
        if(::stat(diskPath.c_str(), &status) != 0 || !S_ISREG(status.st_mode))
            return false;
        outSize = static_cast<u64>(status.st_size);
        outMtimeTicks = static_cast<i64>(status.st_mtime) * 1000000000ll;
#if defined(__APPLE__)
        outMtimeTicks += static_cast<i64>(status.st_mtimespec.tv_nsec);
#elif defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200809L
        outMtimeTicks += static_cast<i64>(status.st_mtim.tv_nsec);
#endif
        return true;
#endif
    }

    [[nodiscard]] static ::Path<MetaArena> ResolvePreludeCachePath(MetaArena& arena){
        u64 rootHash = s_Fnv64OffsetBasis;
#if defined(NWB_PROJECT_SOURCE_DIR)
        rootHash = UpdateFnv64TextExact(rootHash, MStringView(NWB_PROJECT_SOURCE_DIR));
#endif
        const MString fileName = StringFormat(arena, "nwb_adhd_prelude_{:016x}_v{}.bin", rootHash, s_BinaryCacheVersion);

#if defined(FOUNDATION_PLATFORM_WINDOWS)
        wchar tempBuf[MAX_PATH + 2u] = {};
        const DWORD len = GetTempPathW(static_cast<DWORD>(MAX_PATH + 1u), tempBuf);
        if(len > 0u && len <= MAX_PATH){
            ::Path<MetaArena> dir(arena, TStringView(tempBuf, static_cast<usize>(len)));
            return dir / ::Path<MetaArena>(arena, MStringView(fileName.data(), fileName.size()));
        }
        return ::Path<MetaArena>(arena, MStringView(fileName.data(), fileName.size()));
#else
        const auto envTmp = ReadEnvironmentVariable(arena, "TMPDIR");
        const MStringView dirText = (envTmp && *envTmp && !(*envTmp)->empty()) ? MStringView((*envTmp)->data(), (*envTmp)->size()) : MStringView("/tmp");
        ::Path<MetaArena> dir(arena, dirText);
        return dir / ::Path<MetaArena>(arena, MStringView(fileName.data(), fileName.size()));
#endif
    }

    [[nodiscard]] static b8 BuildProjectCandidatePath(const MStringView relativePath, const b8 fromProjectRoot, MetaArena& arena, ::Path<MetaArena>& outDiskPath){
        if(fromProjectRoot){
#if defined(NWB_PROJECT_SOURCE_DIR)
            const MStringView root = NWB_PROJECT_SOURCE_DIR;
            if(!root.empty()){
                MString fullPath(root.data(), root.size(), arena);
                if(fullPath.back() != '/' && fullPath.back() != '\\')
                    fullPath.push_back('/');
                fullPath.append(relativePath.data(), relativePath.size());
                const TString<MetaArena> nativePath = ConvertString(arena, MStringView(fullPath.data(), fullPath.size()));
                outDiskPath = ::Path<MetaArena>(arena, TStringView(nativePath.data(), nativePath.size()));
                return true;
            }
#endif
            return false;
        }
        const TString<MetaArena> nativePath = ConvertString(arena, relativePath);
        outDiskPath = ::Path<MetaArena>(arena, TStringView(nativePath.data(), nativePath.size()));
        return true;
    }

    class PreludeBinaryWriter final{
    public:
        explicit PreludeBinaryWriter(MetaArena& arena)
            : m_arena(arena)
            , m_bytes(arena)
            , m_stringBlob(arena)
            , m_stringOffsets(0, MStringViewHash(), EqualTo<MStringView>(), arena)
            , m_ownedKeys(arena)
        {}


    public:
        template<typename T>
        void writeValue(const T& val){
            static_assert(IsTriviallyCopyable_V<T>, "writeValue requires trivially copyable type");
            const u8* ptr = reinterpret_cast<const u8*>(&val);
            m_bytes.insert(m_bytes.end(), ptr, ptr + sizeof(T));
        }

        void writeBool(const b8 val){
            const u8 b = val ? 1u : 0u;
            writeValue(b);
        }

        void writeRawString(const MStringView sv){
            const u32 len = static_cast<u32>(sv.size());
            writeValue(len);
            if(len > 0u){
                const u8* ptr = reinterpret_cast<const u8*>(sv.data());
                m_bytes.insert(m_bytes.end(), ptr, ptr + len);
            }
        }

        void writeString(const MStringView sv){
            if(sv.empty()){
                const SerializedStringRef emptyRef{ 0u, 0u };
                writeValue(emptyRef);
                return;
            }
            const auto it = m_stringOffsets.find(sv);
            if(it != m_stringOffsets.end()){
                writeValue(it->second);
                return;
            }
            const u32 offset = static_cast<u32>(m_stringBlob.size());
            const u32 length = static_cast<u32>(sv.size());
            m_stringBlob.append(sv.data(), sv.size());
            m_ownedKeys.push_back(MakeGlobalUnique<MString>(m_arena, sv.data(), sv.size(), m_arena));
            const MString& keyStr = *m_ownedKeys.back();
            const SerializedStringRef ref{ offset, length };
            m_stringOffsets.emplace(MStringView(keyStr.data(), keyStr.size()), ref);
            writeValue(ref);
        }

        [[nodiscard]] const Vector<u8, MetaArena>& payloadBytes()const noexcept{ return m_bytes; }
        [[nodiscard]] const MString& stringBlob()const noexcept{ return m_stringBlob; }


    private:
        MetaArena& m_arena;
        Vector<u8, MetaArena> m_bytes;
        MString m_stringBlob;
        HashMap<MStringView, SerializedStringRef, MetaArena, MStringViewHash> m_stringOffsets;
        MVector<GlobalUniquePtr<MString>> m_ownedKeys;
    };

    class PreludeBinaryReader final{
    public:
        PreludeBinaryReader(const u8* data, const usize size)noexcept
            : m_data(data)
            , m_size(size)
        {}


    public:
        [[nodiscard]] b8 hasRemaining(const usize byteCount)const noexcept{
            return m_cursor <= m_size && byteCount <= (m_size - m_cursor);
        }

        template<typename T>
        [[nodiscard]] b8 readValue(T& outVal)noexcept{
            static_assert(IsTriviallyCopyable_V<T>, "readValue requires trivially copyable type");
            if(!hasRemaining(sizeof(T)))
                return false;
            FOUNDATION_MEMCPY(&outVal, sizeof(T), m_data + m_cursor, sizeof(T));
            m_cursor += sizeof(T);
            return true;
        }

        [[nodiscard]] b8 readBool(b8& outVal)noexcept{
            u8 b = 0u;
            if(!readValue(b))
                return false;
            outVal = (b != 0u);
            return true;
        }

        [[nodiscard]] b8 readRawStringView(MStringView& outView)noexcept{
            u32 len = 0u;
            if(!readValue(len))
                return false;
            if(!hasRemaining(len))
                return false;
            outView = MStringView(reinterpret_cast<const char*>(m_data + m_cursor), len);
            m_cursor += len;
            return true;
        }

        [[nodiscard]] b8 readStringRef(const MString& blob, MStringView& outView)noexcept{
            SerializedStringRef ref{};
            if(!readValue(ref))
                return false;
            if(ref.length == 0u){
                outView = {};
                return true;
            }
            if(static_cast<usize>(ref.offset) > blob.size() || static_cast<usize>(ref.length) > (blob.size() - static_cast<usize>(ref.offset)))
                return false;
            outView = MStringView(blob.data() + ref.offset, ref.length);
            return true;
        }

        [[nodiscard]] b8 readBytes(void* dst, const usize byteCount)noexcept{
            if(!hasRemaining(byteCount))
                return false;
            if(byteCount > 0u)
                FOUNDATION_MEMCPY(dst, byteCount, m_data + m_cursor, byteCount);
            m_cursor += byteCount;
            return true;
        }

        [[nodiscard]] b8 atEnd()const noexcept{ return m_cursor == m_size; }


    private:
        const u8* m_data = nullptr;
        usize m_size = 0u;
        usize m_cursor = 0u;
    };


public:
    CoreExternRegistry()
        : m_arena(MetascriptArenaScope::s_ParserScratch)
        , m_sourceText(m_arena)
        , m_loadError(m_arena)
        , m_ownedStrings(m_arena)
        , m_ownedTypeRefs(m_arena)
        , m_ownedTemplateParamLists(m_arena)
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
        , m_scopedFunctionByKey(m_arena)
        , m_globalConstantByName(m_arena)
        , m_scopedConstantByKey(m_arena)
        , m_simdFieldTypes(m_arena)
        , m_exactRuleByKey(m_arena)
        , m_visitorRuleByKey(m_arena)
        , m_polymorphicDecls(m_arena)
        , m_polymorphicMethods(m_arena)
        , m_polymorphicDeclByName(m_arena)
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
        return m_enumEntryByKey.find(StringViewPair{ enumName, entryName }) != m_enumEntryByKey.end();
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
        if(memberName.empty())
            return nullptr;
        const MStringView prefix = ResolveBuiltinScopePrefix(scopeTypeName);
        if(prefix.empty())
            return nullptr;
        return FindPairInMap(m_functions, m_scopedFunctionByKey, prefix, memberName);
    }

    [[nodiscard]] const BuiltinGlobalConstantInfo* findGlobalConstant(const MStringView name)const noexcept{
        return FindInMap(m_globalConstants, m_globalConstantByName, name);
    }

    [[nodiscard]] const BuiltinGlobalConstantInfo* findScopedConstant(const MStringView scopeTypeName, const MStringView memberName)const noexcept{
        if(memberName.empty())
            return nullptr;
        const MStringView prefix = ResolveBuiltinScopePrefix(scopeTypeName);
        if(prefix.empty())
            return nullptr;
        return FindPairInMap(m_globalConstants, m_scopedConstantByKey, prefix, memberName);
    }

    [[nodiscard]] b8 hasSimdRegisterField(const MStringView typeName)const noexcept{
        return m_simdFieldTypes.find(typeName) != m_simdFieldTypes.end();
    }

    [[nodiscard]] const ClassDecl* findPolymorphicInterface(const MStringView name)const noexcept{
        const auto it = m_polymorphicDeclByName.find(name);
        if(it == m_polymorphicDeclByName.end())
            return nullptr;
        return m_polymorphicDecls[it->second].get();
    }


private:
    struct WatchedPreludeList{
        MVector<WatchedPreludeFile> files;
        MetaArena& arena;


    public:
        explicit WatchedPreludeList(MetaArena& inArena)noexcept
            : files(inArena)
            , arena(inArena)
        {}
    };

    class PreludeIncludeResolver final : public IIncludeResolver{
    public:
        PreludeIncludeResolver(CoreExternRegistry& owner, WatchedPreludeList* watchedFiles)noexcept
            : m_owner(owner)
            , m_watchedFiles(watchedFiles)
        {}


    public:
        virtual b8 resolveInclude(const MStringView includePath, const b8, MetaArena& arena, MString& outResolvedKey, MString& outContent)override{
            if(includePath.empty())
                return false;
            outResolvedKey.assign(includePath.data(), includePath.size());
            return m_owner.tryReadProjectFile(includePath, arena, outContent, m_watchedFiles);
        }


    private:
        CoreExternRegistry& m_owner;
        WatchedPreludeList* m_watchedFiles = nullptr;
    };

    [[nodiscard]] b8 tryReadProjectFile(const MStringView relativePath, MetaArena& arena, MString& outText, WatchedPreludeList* watchedList = nullptr)const{
        const auto recordWatched = [&](const ::Path<MetaArena>& diskPath, const b8 fromRoot){
            if(!watchedList)
                return;
            u64 fileSize = 0u;
            i64 mtimeTicks = 0;
            if(!QueryDiskFileStat(diskPath, fileSize, mtimeTicks))
                return;
            for(const WatchedPreludeFile& existing : watchedList->files){
                if(existing.fromProjectRoot == fromRoot && existing.relativePath == relativePath)
                    return;
            }
            WatchedPreludeFile entry(watchedList->arena);
            entry.relativePath.assign(relativePath.data(), relativePath.size());
            entry.fileSize = fileSize;
            entry.mtimeTicks = mtimeTicks;
            entry.fromProjectRoot = fromRoot;
            watchedList->files.push_back(Move(entry));
        };

        const auto tryReadPath = [&](const MStringView pathText, const b8 fromRoot) -> b8{
            const TString<MetaArena> nativePath = ConvertString(arena, pathText);
            const ::Path<MetaArena> diskPath(arena, TStringView(nativePath.data(), nativePath.size()));
            outText.clear();
            if(!ReadTextFile(diskPath, outText))
                return false;
            recordWatched(diskPath, fromRoot);
            return true;
        };

#if defined(NWB_PROJECT_SOURCE_DIR)
        {
            const MStringView root = NWB_PROJECT_SOURCE_DIR;
            if(!root.empty()){
                MString fullPath(root.data(), root.size(), arena);
                if(fullPath.back() != '/' && fullPath.back() != '\\')
                    fullPath.push_back('/');
                fullPath.append(relativePath.data(), relativePath.size());
                if(tryReadPath(MStringView(fullPath.data(), fullPath.size()), true))
                    return true;
            }
        }
#endif
        return tryReadPath(relativePath, false);
    }

    template<typename InfoT>
    [[nodiscard]] static const InfoT* FindInMap(const MVector<InfoT>& items, const HashMap<MStringView, usize, MetaArena, MStringViewHash>& indexMap, const MStringView key)noexcept{
        const auto it = indexMap.find(key);
        return (it != indexMap.end()) ? &items[it->second] : nullptr;
    }

    template<typename InfoT>
    [[nodiscard]] static const InfoT* FindPairInMap(const MVector<InfoT>& items, const HashMap<StringViewPair, usize, MetaArena, StringViewPairHash>& indexMap, const MStringView a, const MStringView b)noexcept{
        const auto it = indexMap.find(StringViewPair{ a, b });
        return (it != indexMap.end()) ? &items[it->second] : nullptr;
    }

    void insertUniqueIndex(HashMap<MStringView, usize, MetaArena, MStringViewHash>& map, const MStringView key, const usize idx){
        if(!key.empty())
            map.emplace(key, idx);
    }

    void insertUniquePairIndex(HashMap<StringViewPair, usize, MetaArena, StringViewPairHash>& map, const MStringView a, const MStringView b, const usize idx){
        if(!a.empty() && !b.empty())
            map.emplace(StringViewPair{ a, b }, idx);
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

    [[nodiscard]] TypeRef* cloneTypeRef(const TypeRef* const src){
        if(!src)
            return nullptr;
        m_ownedTypeRefs.push_back(MakeGlobalUnique<TypeRef>(m_arena, m_arena));
        TypeRef* const dst = m_ownedTypeRefs.back().get();
        dst->name = ownString(src->name);
        dst->refMode = src->refMode;
        dst->inferredCaptureBytes = src->inferredCaptureBytes;
        dst->span = src->span;
        dst->isFuncType = src->isFuncType;
        dst->isAuto = src->isAuto;
        dst->templateArgs.reserve(src->templateArgs.size());
        for(const TypeArg& arg : src->templateArgs)
            dst->templateArgs.push_back(TypeArg{ cloneTypeRef(arg.type), ownString(arg.constantText) });
        dst->funcReturnType = cloneTypeRef(src->funcReturnType);
        dst->funcParams.reserve(src->funcParams.size());
        for(const FuncParamType& fp : src->funcParams)
            dst->funcParams.push_back(FuncParamType{ fp.mode, cloneTypeRef(fp.type) });
        return dst;
    }

    [[nodiscard]] Span<const TemplateParamDecl> cloneTemplateParams(const Span<const TemplateParamDecl> src){
        if(src.empty())
            return {};
        m_ownedTemplateParamLists.push_back(MakeGlobalUnique<MVector<TemplateParamDecl>>(m_arena, m_arena));
        MVector<TemplateParamDecl>& dstList = *m_ownedTemplateParamLists.back();
        dstList.reserve(src.size());
        for(const TemplateParamDecl& tp : src){
            dstList.emplace_back(m_arena);
            TemplateParamDecl& dst = dstList.back();
            dst.name = ownString(tp.name);
            dst.constValueType = cloneTypeRef(tp.constValueType);
            dst.interfaceBounds.reserve(tp.interfaceBounds.size());
            for(const TypeRef* const bound : tp.interfaceBounds)
                dst.interfaceBounds.push_back(cloneTypeRef(bound));
            dst.span = tp.span;
        }
        return Span<const TemplateParamDecl>(dstList.data(), dstList.size());
    }

    MString buildPairKey(const MStringView a, const MStringView b){
        MString key(m_arena);
        key.reserve(a.size() + 1u + b.size());
        key.append(a.data(), a.size());
        key.push_back('\n');
        key.append(b.data(), b.size());
        return key;
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
                m_enumEntryByKey.insert(StringViewPair{ info.adhdName, ownedEntry });
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
            obj.templateParams = cloneTemplateParams(cls->templateParams);

            const usize objIdx = m_objectTypes.size();
            m_objectTypes.push_back(obj);
            insertUniqueIndex(m_objectTypeByName, obj.adhdName, objIdx);

            ClassDecl* polyDecl = nullptr;
            if(cls->isPolymorphicBase){
                m_polymorphicDecls.push_back(MakeGlobalUnique<ClassDecl>(m_arena, m_arena));
                polyDecl = m_polymorphicDecls.back().get();
                polyDecl->name = obj.adhdName;
                polyDecl->isExtern = true;
                polyDecl->isPolymorphicBase = true;
                insertUniqueIndex(m_polymorphicDeclByName, obj.adhdName, m_polymorphicDecls.size() - 1u);
            }

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
                insertUniquePairIndex(m_fieldByKey, obj.adhdName, fInfo.fieldName, fIdx);
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
                insertUniquePairIndex(m_receiverMethodByKey, obj.adhdName, rm.methodName, rmIdx);

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
                    insertUniquePairIndex(m_receiverSigByKey, obj.adhdName, rm.methodName, sigIdx);
                }

                if(polyDecl){
                    m_polymorphicMethods.push_back(MakeGlobalUnique<MethodDecl>(m_arena, m_arena));
                    MethodDecl* const mCopy = m_polymorphicMethods.back().get();
                    mCopy->name = rm.methodName;
                    mCopy->isConst = method->isConst;
                    mCopy->isVariadic = method->isVariadic;
                    mCopy->allowsCppUnwind = method->allowsCppUnwind;
                    mCopy->returnType = cloneTypeRef(method->returnType);
                    mCopy->params.reserve(method->params.size());
                    for(const ParamDecl& p : method->params){
                        ParamDecl pCopy{};
                        pCopy.mode = p.mode;
                        pCopy.name = ownString(p.name);
                        pCopy.type = cloneTypeRef(p.type);
                        pCopy.cppConstReference = p.cppConstReference;
                        mCopy->params.push_back(Move(pCopy));
                    }
                    polyDecl->methods.push_back(mCopy);
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
            IndexScopedSymbolKey(m_scopedFunctionByKey, info.cppExportName, fnIdx, true);
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
            IndexScopedSymbolKey(m_scopedConstantByKey, gc.cppExportName, gcIdx, false);
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

    void resetRegistryState(){
        m_sourceText.clear();
        m_loadError.clear();
        m_ownedStrings.clear();
        m_ownedTypeRefs.clear();
        m_ownedTemplateParamLists.clear();
        m_objectTypes.clear();
        m_enums.clear();
        m_enumEntries.clear();
        m_fields.clear();
        m_receiverMethods.clear();
        m_receiverSignatures.clear();
        m_argRules.clear();
        m_visitorRules.clear();
        m_functions.clear();
        m_globalConstants.clear();
        m_objectTypeByName.clear();
        m_enumByName.clear();
        m_enumEntryByKey.clear();
        m_fieldByKey.clear();
        m_receiverMethodByKey.clear();
        m_receiverSigByKey.clear();
        m_functionByName.clear();
        m_scopedFunctionByKey.clear();
        m_globalConstantByName.clear();
        m_scopedConstantByKey.clear();
        m_simdFieldTypes.clear();
        m_exactRuleByKey.clear();
        m_visitorRuleByKey.clear();
        m_polymorphicDecls.clear();
        m_polymorphicMethods.clear();
        m_polymorphicDeclByName.clear();
    }

    void rebuildLookupIndices(){
        for(usize i = 0u; i < m_enums.size(); ++i)
            insertUniqueIndex(m_enumByName, m_enums[i].adhdName, i);
        for(const BuiltinEnumEntryInfo& entry : m_enumEntries)
            m_enumEntryByKey.insert(StringViewPair{ entry.adhdEnumName, entry.entryName });
        for(usize i = 0u; i < m_objectTypes.size(); ++i)
            insertUniqueIndex(m_objectTypeByName, m_objectTypes[i].adhdName, i);
        for(usize i = 0u; i < m_fields.size(); ++i){
            const BuiltinFieldInfo& fInfo = m_fields[i];
            insertUniquePairIndex(m_fieldByKey, fInfo.receiverTypeName, fInfo.fieldName, i);
            if(SymbolTable::IsSimdRegisterType(fInfo.fieldTypeName) && m_simdFieldTypes.find(fInfo.receiverTypeName) == m_simdFieldTypes.end())
                m_simdFieldTypes.emplace(MString(fInfo.receiverTypeName.data(), fInfo.receiverTypeName.size(), m_arena), 1u);
        }
        for(usize i = 0u; i < m_receiverMethods.size(); ++i)
            insertUniquePairIndex(m_receiverMethodByKey, m_receiverMethods[i].receiverTypeName, m_receiverMethods[i].methodName, i);
        for(usize i = 0u; i < m_receiverSignatures.size(); ++i)
            insertUniquePairIndex(m_receiverSigByKey, m_receiverSignatures[i].receiverTypeName, m_receiverSignatures[i].methodName, i);
        for(usize i = 0u; i < m_functions.size(); ++i){
            insertUniqueIndex(m_functionByName, m_functions[i].adhdName, i);
            IndexScopedSymbolKey(m_scopedFunctionByKey, m_functions[i].cppExportName, i, true);
        }
        for(usize i = 0u; i < m_globalConstants.size(); ++i){
            insertUniqueIndex(m_globalConstantByName, m_globalConstants[i].adhdName, i);
            IndexScopedSymbolKey(m_scopedConstantByKey, m_globalConstants[i].cppExportName, i, false);
        }
        for(usize i = 0u; i < m_polymorphicDecls.size(); ++i)
            insertUniqueIndex(m_polymorphicDeclByName, m_polymorphicDecls[i]->name, i);
    }

    [[nodiscard]] b8 tryLoadBinaryCache(MetaArena& scratchArena){
        const ::Path<MetaArena> cachePath = ResolvePreludeCachePath(scratchArena);
        Vector<u8, MetaArena> fileBytes(scratchArena);
        if(!ReadBinaryFile(cachePath, fileBytes) || fileBytes.size() < 20u)
            return false;

        PreludeBinaryReader reader(fileBytes.data(), fileBytes.size());
        u32 magic = 0u;
        u32 version = 0u;
        u64 schemaSig = 0u;
        u32 watchedCount = 0u;
        if(!reader.readValue(magic) || !reader.readValue(version) || !reader.readValue(schemaSig) || !reader.readValue(watchedCount))
            return false;
        if(magic != s_BinaryCacheMagic || version != s_BinaryCacheVersion || schemaSig != ComputeBinaryCacheSchemaSignature())
            return false;
        if(watchedCount < 2u || watchedCount > 4096u)
            return false;

        ::Path<MetaArena> candidateDiskPath(scratchArena);
        for(u32 wi = 0u; wi < watchedCount; ++wi){
            MStringView relPath;
            u64 expectedSize = 0u;
            i64 expectedMtime = 0;
            b8 fromRoot = false;
            if(!reader.readRawStringView(relPath) || !reader.readValue(expectedSize) || !reader.readValue(expectedMtime) || !reader.readBool(fromRoot))
                return false;
            if(!BuildProjectCandidatePath(relPath, fromRoot, scratchArena, candidateDiskPath))
                return false;
            u64 actualSize = 0u;
            i64 actualMtime = 0;
            if(!QueryDiskFileStat(candidateDiskPath, actualSize, actualMtime))
                return false;
            if(actualSize != expectedSize || actualMtime != expectedMtime)
                return false;
        }

        u32 stringBlobSize = 0u;
        if(!reader.readValue(stringBlobSize) || !reader.hasRemaining(stringBlobSize))
            return false;

        resetRegistryState();
        m_sourceText.resize(stringBlobSize);
        if(!reader.readBytes(m_sourceText.data(), stringBlobSize)){
            resetRegistryState();
            return false;
        }

        const auto resolveTypePtr = [&](const i32 idx)noexcept -> TypeRef*{
            if(idx < 0 || static_cast<usize>(idx) >= m_ownedTypeRefs.size())
                return nullptr;
            return m_ownedTypeRefs[static_cast<usize>(idx)].get();
        };

        const auto readTypeRefAndPopulate = [&]() -> b8{
            u32 typeRefCount = 0u;
            if(!reader.readValue(typeRefCount) || typeRefCount > 65536u)
                return false;
            m_ownedTypeRefs.reserve(typeRefCount);
            for(u32 i = 0u; i < typeRefCount; ++i)
                m_ownedTypeRefs.push_back(MakeGlobalUnique<TypeRef>(m_arena, m_arena));

            for(u32 i = 0u; i < typeRefCount; ++i){
                TypeRef& tr = *m_ownedTypeRefs[i];
                u8 refModeByte = 0u;
                u64 captureBytes = 0u;
                i32 funcRetIdx = -1;
                u32 tplArgCount = 0u;
                u32 funcParamCount = 0u;
                if(!reader.readStringRef(m_sourceText, tr.name)
                    || !reader.readValue(refModeByte)
                    || !reader.readValue(captureBytes)
                    || !reader.readValue(tr.span)
                    || !reader.readBool(tr.isFuncType)
                    || !reader.readBool(tr.isAuto)
                    || !reader.readValue(tplArgCount)
                    || !reader.readValue(funcRetIdx)
                    || !reader.readValue(funcParamCount)
                )
                    return false;
                if(tplArgCount > 1024u || funcParamCount > 1024u)
                    return false;
                tr.refMode = static_cast<ParamPassingMode::Enum>(refModeByte);
                tr.inferredCaptureBytes = static_cast<usize>(captureBytes);
                tr.funcReturnType = resolveTypePtr(funcRetIdx);
                tr.templateArgs.reserve(tplArgCount);
                for(u32 a = 0u; a < tplArgCount; ++a){
                    i32 argTypeIdx = -1;
                    MStringView constText;
                    if(!reader.readValue(argTypeIdx) || !reader.readStringRef(m_sourceText, constText))
                        return false;
                    tr.templateArgs.push_back(TypeArg{ resolveTypePtr(argTypeIdx), constText });
                }
                tr.funcParams.reserve(funcParamCount);
                for(u32 p = 0u; p < funcParamCount; ++p){
                    u8 pModeByte = 0u;
                    i32 pTypeIdx = -1;
                    if(!reader.readValue(pModeByte) || !reader.readValue(pTypeIdx))
                        return false;
                    tr.funcParams.push_back(FuncParamType{ static_cast<ParamPassingMode::Enum>(pModeByte), resolveTypePtr(pTypeIdx) });
                }
            }

            u32 tplListCount = 0u;
            if(!reader.readValue(tplListCount) || tplListCount > 65536u)
                return false;
            m_ownedTemplateParamLists.reserve(tplListCount);
            for(u32 li = 0u; li < tplListCount; ++li){
                u32 paramCount = 0u;
                if(!reader.readValue(paramCount) || paramCount > 1024u)
                    return false;
                m_ownedTemplateParamLists.push_back(MakeGlobalUnique<MVector<TemplateParamDecl>>(m_arena, m_arena));
                MVector<TemplateParamDecl>& list = *m_ownedTemplateParamLists.back();
                list.reserve(paramCount);
                for(u32 pi = 0u; pi < paramCount; ++pi){
                    list.emplace_back(m_arena);
                    TemplateParamDecl& tp = list.back();
                    i32 constTypeIdx = -1;
                    u32 boundCount = 0u;
                    if(!reader.readStringRef(m_sourceText, tp.name)
                        || !reader.readValue(constTypeIdx)
                        || !reader.readValue(tp.span)
                        || !reader.readValue(boundCount)
                        || boundCount > 1024u
                    )
                        return false;
                    tp.constValueType = resolveTypePtr(constTypeIdx);
                    tp.interfaceBounds.reserve(boundCount);
                    for(u32 bi = 0u; bi < boundCount; ++bi){
                        i32 boundIdx = -1;
                        if(!reader.readValue(boundIdx))
                            return false;
                        tp.interfaceBounds.push_back(resolveTypePtr(boundIdx));
                    }
                }
            }
            return true;
        };

        if(!readTypeRefAndPopulate()){
            resetRegistryState();
            return false;
        }

        u32 objCount = 0u;
        if(!reader.readValue(objCount) || objCount > 65536u){
            resetRegistryState();
            return false;
        }
        m_objectTypes.reserve(objCount);
        for(u32 i = 0u; i < objCount; ++i){
            BuiltinObjectTypeInfo obj{};
            i32 tplListIdx = -1;
            if(!reader.readStringRef(m_sourceText, obj.adhdName)
                || !reader.readStringRef(m_sourceText, obj.cppExportName)
                || !reader.readStringRef(m_sourceText, obj.requiredHeader)
                || !reader.readValue(obj.byteSize)
                || !reader.readValue(obj.byteAlign)
                || !reader.readBool(obj.isArenaBacked)
                || !reader.readBool(obj.isSelfDefaulted)
                || !reader.readBool(obj.isSmallValue)
                || !reader.readBool(obj.isBorrowView)
                || !reader.readBool(obj.isBinarySerializable)
                || !reader.readBool(obj.isSingleValueCtor)
                || !reader.readBool(obj.emitsBraceFieldInit)
                || !reader.readBool(obj.isStringLike)
                || !reader.readBool(obj.isStringSwitch)
                || !reader.readBool(obj.isNonThreadSafeContainer)
                || !reader.readBool(obj.isNwbMapKey)
                || !reader.readBool(obj.requiresTCharAdaptation)
                || !reader.readValue(obj.arenaCtorMode)
                || !reader.readBool(obj.isCopyMoveConstructible)
                || !reader.readBool(obj.isAssignable)
                || !reader.readBool(obj.isAbstract)
                || !reader.readBool(obj.isPolymorphicBase)
                || !reader.readValue(obj.formatArgWrapKind)
                || !reader.readValue(tplListIdx)
            ){
                resetRegistryState();
                return false;
            }
            if(tplListIdx >= 0 && static_cast<usize>(tplListIdx) < m_ownedTemplateParamLists.size()){
                const MVector<TemplateParamDecl>& list = *m_ownedTemplateParamLists[static_cast<usize>(tplListIdx)];
                obj.templateParams = Span<const TemplateParamDecl>(list.data(), list.size());
            }
            m_objectTypes.push_back(obj);
        }

        u32 enumCount = 0u;
        if(!reader.readValue(enumCount) || enumCount > 65536u){
            resetRegistryState();
            return false;
        }
        m_enums.reserve(enumCount);
        for(u32 i = 0u; i < enumCount; ++i){
            BuiltinEnumInfo en{};
            if(!reader.readStringRef(m_sourceText, en.adhdName)
                || !reader.readStringRef(m_sourceText, en.cppTypeName)
                || !reader.readStringRef(m_sourceText, en.cppScopeName)
                || !reader.readStringRef(m_sourceText, en.requiredHeader)
                || !reader.readValue(en.byteSize)
                || !reader.readValue(en.byteAlign)
                || !reader.readBool(en.isFlag)
            ){
                resetRegistryState();
                return false;
            }
            m_enums.push_back(en);
        }

        u32 enumEntryCount = 0u;
        if(!reader.readValue(enumEntryCount) || enumEntryCount > 262144u){
            resetRegistryState();
            return false;
        }
        m_enumEntries.reserve(enumEntryCount);
        for(u32 i = 0u; i < enumEntryCount; ++i){
            BuiltinEnumEntryInfo entry{};
            if(!reader.readStringRef(m_sourceText, entry.adhdEnumName) || !reader.readStringRef(m_sourceText, entry.entryName)){
                resetRegistryState();
                return false;
            }
            m_enumEntries.push_back(entry);
        }

        u32 fieldCount = 0u;
        if(!reader.readValue(fieldCount) || fieldCount > 262144u){
            resetRegistryState();
            return false;
        }
        m_fields.reserve(fieldCount);
        for(u32 i = 0u; i < fieldCount; ++i){
            BuiltinFieldInfo field{};
            if(!reader.readStringRef(m_sourceText, field.receiverTypeName)
                || !reader.readStringRef(m_sourceText, field.fieldName)
                || !reader.readStringRef(m_sourceText, field.fieldTypeName)
                || !reader.readStringRef(m_sourceText, field.elemTypeName)
                || !reader.readStringRef(m_sourceText, field.countText)
            ){
                resetRegistryState();
                return false;
            }
            m_fields.push_back(field);
        }

        u32 rmCount = 0u;
        if(!reader.readValue(rmCount) || rmCount > 262144u){
            resetRegistryState();
            return false;
        }
        m_receiverMethods.reserve(rmCount);
        for(u32 i = 0u; i < rmCount; ++i){
            BuiltinReceiverMethodInfo rm{};
            u8 retArgRefModeByte = 0u;
            if(!reader.readStringRef(m_sourceText, rm.receiverTypeName)
                || !reader.readStringRef(m_sourceText, rm.methodName)
                || !reader.readStringRef(m_sourceText, rm.returnTypeName)
                || !reader.readStringRef(m_sourceText, rm.returnTypeArgName)
                || !reader.readStringRef(m_sourceText, rm.returnNestedTypeArgName)
                || !reader.readStringRef(m_sourceText, rm.returnErrorTypeName)
                || !reader.readBool(rm.synthesizeTypeRef)
                || !reader.readBool(rm.isMutating)
                || !reader.readBool(rm.returnsReference)
                || !reader.readValue(retArgRefModeByte)
            ){
                resetRegistryState();
                return false;
            }
            rm.returnTypeArgRefMode = static_cast<ParamPassingMode::Enum>(retArgRefModeByte);
            m_receiverMethods.push_back(rm);
        }

        u32 sigCount = 0u;
        if(!reader.readValue(sigCount) || sigCount > 262144u){
            resetRegistryState();
            return false;
        }
        m_receiverSignatures.reserve(sigCount);
        for(u32 i = 0u; i < sigCount; ++i){
            BuiltinReceiverSignatureInfo sig{};
            if(!reader.readStringRef(m_sourceText, sig.receiverTypeName) || !reader.readStringRef(m_sourceText, sig.methodName)){
                resetRegistryState();
                return false;
            }
            for(BuiltinFunctionArgRule::Enum& r : sig.argRules){
                u16 rv = 0u;
                if(!reader.readValue(rv)){
                    resetRegistryState();
                    return false;
                }
                r = static_cast<BuiltinFunctionArgRule::Enum>(rv);
            }
            if(!reader.readValue(sig.arity)
                || !reader.readValue(sig.retainedArgMask)
                || !reader.readValue(sig.cppConstReferenceMask)
                || !reader.readBool(sig.allowsCppUnwind)
            ){
                resetRegistryState();
                return false;
            }
            m_receiverSignatures.push_back(sig);
        }

        u32 ruleCount = 0u;
        if(!reader.readValue(ruleCount) || ruleCount > 65536u){
            resetRegistryState();
            return false;
        }
        m_argRules.reserve(ruleCount);
        for(u32 i = 0u; i < ruleCount; ++i){
            BuiltinFunctionArgRuleInfo r{};
            u16 rv = 0u;
            if(!reader.readValue(rv)
                || !reader.readStringRef(m_sourceText, r.expectedParamType)
                || !reader.readStringRef(m_sourceText, r.requiredExactType)
                || !reader.readStringRef(m_sourceText, r.errorMessage)
                || !reader.readBool(r.isMut)
                || !reader.readBool(r.requiresStringLike)
                || !reader.readBool(r.requiresIntegerScalar)
            ){
                resetRegistryState();
                return false;
            }
            r.rule = static_cast<BuiltinFunctionArgRule::Enum>(rv);
            m_argRules.push_back(r);
        }

        u32 visitorCount = 0u;
        if(!reader.readValue(visitorCount) || visitorCount > 65536u){
            resetRegistryState();
            return false;
        }
        m_visitorRules.reserve(visitorCount);
        for(u32 i = 0u; i < visitorCount; ++i){
            BuiltinVisitorRuleInfo vr{};
            u16 rv = 0u;
            if(!reader.readValue(rv)
                || !reader.readStringRef(m_sourceText, vr.valueType)
                || !reader.readStringRef(m_sourceText, vr.keyType)
                || !reader.readBool(vr.mutableWithReceiver)
            ){
                resetRegistryState();
                return false;
            }
            vr.rule = static_cast<BuiltinFunctionArgRule::Enum>(rv);
            m_visitorRules.push_back(vr);
        }

        u32 fnCount = 0u;
        if(!reader.readValue(fnCount) || fnCount > 65536u){
            resetRegistryState();
            return false;
        }
        m_functions.reserve(fnCount);
        for(u32 i = 0u; i < fnCount; ++i){
            BuiltinFunctionInfo fn{};
            u64 minArity = 0u;
            u64 arity = 0u;
            u8 retRuleByte = 0u;
            u8 callLowerByte = 0u;
            if(!reader.readStringRef(m_sourceText, fn.adhdName)
                || !reader.readStringRef(m_sourceText, fn.cppExportName)
                || !reader.readStringRef(m_sourceText, fn.returnTypeName)
                || !reader.readStringRef(m_sourceText, fn.returnTypeArgName)
                || !reader.readStringRef(m_sourceText, fn.returnNestedTypeArgName)
                || !reader.readStringRef(m_sourceText, fn.returnErrorTypeName)
                || !reader.readStringRef(m_sourceText, fn.requiredHeader)
                || !reader.readValue(minArity)
                || !reader.readValue(arity)
                || !reader.readValue(retRuleByte)
                || !reader.readValue(callLowerByte)
                || !reader.readBool(fn.prependsScratchArena)
                || !reader.readBool(fn.isVariadic)
                || !reader.readBool(fn.wrapsFormatArgs)
                || !reader.readBool(fn.requiresExplicitTypeArg)
                || !reader.readBool(fn.returnsBorrowedView)
                || !reader.readBool(fn.rejectsSimdStorage)
                || !reader.readBool(fn.returnsReference)
            ){
                resetRegistryState();
                return false;
            }
            fn.minArity = static_cast<usize>(minArity);
            fn.arity = static_cast<usize>(arity);
            fn.returnRule = static_cast<BuiltinFunctionReturnRule::Enum>(retRuleByte);
            fn.callLowering = static_cast<BuiltinCallLowering::Enum>(callLowerByte);
            for(BuiltinFunctionArgRule::Enum& r : fn.argRules){
                u16 rv = 0u;
                if(!reader.readValue(rv)){
                    resetRegistryState();
                    return false;
                }
                r = static_cast<BuiltinFunctionArgRule::Enum>(rv);
            }
            if(!reader.readValue(fn.retainedArgMask) || !reader.readValue(fn.explicitTypeParamMask)){
                resetRegistryState();
                return false;
            }
            m_functions.push_back(fn);
        }

        u32 gcCount = 0u;
        if(!reader.readValue(gcCount) || gcCount > 65536u){
            resetRegistryState();
            return false;
        }
        m_globalConstants.reserve(gcCount);
        for(u32 i = 0u; i < gcCount; ++i){
            BuiltinGlobalConstantInfo gc{};
            if(!reader.readStringRef(m_sourceText, gc.adhdName)
                || !reader.readStringRef(m_sourceText, gc.cppExportName)
                || !reader.readStringRef(m_sourceText, gc.adhdTypeName)
                || !reader.readStringRef(m_sourceText, gc.requiredHeader)
            ){
                resetRegistryState();
                return false;
            }
            m_globalConstants.push_back(gc);
        }

        u32 polyMethodCount = 0u;
        if(!reader.readValue(polyMethodCount) || polyMethodCount > 65536u){
            resetRegistryState();
            return false;
        }
        m_polymorphicMethods.reserve(polyMethodCount);
        for(u32 i = 0u; i < polyMethodCount; ++i){
            m_polymorphicMethods.push_back(MakeGlobalUnique<MethodDecl>(m_arena, m_arena));
            MethodDecl* const method = m_polymorphicMethods.back().get();
            i32 returnTypeIdx = -1;
            u32 paramCount = 0u;
            if(!reader.readStringRef(m_sourceText, method->name)
                || !reader.readBool(method->isConst)
                || !reader.readBool(method->isVariadic)
                || !reader.readBool(method->allowsCppUnwind)
                || !reader.readValue(returnTypeIdx)
                || !reader.readValue(paramCount)
                || paramCount > 1024u
            ){
                resetRegistryState();
                return false;
            }
            method->returnType = resolveTypePtr(returnTypeIdx);
            method->params.reserve(paramCount);
            for(u32 pi = 0u; pi < paramCount; ++pi){
                ParamDecl param{};
                u8 modeByte = 0u;
                i32 paramTypeIdx = -1;
                if(!reader.readValue(modeByte)
                    || !reader.readStringRef(m_sourceText, param.name)
                    || !reader.readValue(paramTypeIdx)
                    || !reader.readBool(param.cppConstReference)
                ){
                    resetRegistryState();
                    return false;
                }
                param.mode = static_cast<ParamPassingMode::Enum>(modeByte);
                param.type = resolveTypePtr(paramTypeIdx);
                method->params.push_back(Move(param));
            }
        }

        u32 polyDeclCount = 0u;
        if(!reader.readValue(polyDeclCount) || polyDeclCount > 65536u){
            resetRegistryState();
            return false;
        }
        m_polymorphicDecls.reserve(polyDeclCount);
        for(u32 i = 0u; i < polyDeclCount; ++i){
            m_polymorphicDecls.push_back(MakeGlobalUnique<ClassDecl>(m_arena, m_arena));
            ClassDecl* const cls = m_polymorphicDecls.back().get();
            u32 methodCount = 0u;
            if(!reader.readStringRef(m_sourceText, cls->name)
                || !reader.readBool(cls->isExtern)
                || !reader.readBool(cls->isPolymorphicBase)
                || !reader.readValue(methodCount)
                || methodCount > 65536u
            ){
                resetRegistryState();
                return false;
            }
            cls->methods.reserve(methodCount);
            for(u32 mi = 0u; mi < methodCount; ++mi){
                u32 methodIdx = 0u;
                if(!reader.readValue(methodIdx) || methodIdx >= m_polymorphicMethods.size()){
                    resetRegistryState();
                    return false;
                }
                cls->methods.push_back(m_polymorphicMethods[methodIdx].get());
            }
        }

        if(!reader.atEnd()){
            resetRegistryState();
            return false;
        }

        rebuildLookupIndices();
        return true;
    }

    void saveBinaryCache(const WatchedPreludeList& watchedList, MetaArena& scratchArena)const{
        if(watchedList.files.size() < 2u || !m_loadError.empty())
            return;

        PreludeBinaryWriter writer(scratchArena);

        MVector<const TypeRef*> flatTypeRefs(scratchArena);
        HashMap<const TypeRef*, i32, MetaArena> typeRefIndices(scratchArena);
        const auto registerTypeRef = [&](const auto& self, const TypeRef* const tr) -> void{
            if(!tr || typeRefIndices.find(tr) != typeRefIndices.end())
                return;
            const i32 idx = static_cast<i32>(flatTypeRefs.size());
            flatTypeRefs.push_back(tr);
            typeRefIndices.emplace(tr, idx);
            for(const TypeArg& arg : tr->templateArgs)
                self(self, arg.type);
            self(self, tr->funcReturnType);
            for(const FuncParamType& fp : tr->funcParams)
                self(self, fp.type);
        };

        for(const auto& tpListPtr : m_ownedTemplateParamLists){
            for(const TemplateParamDecl& tp : *tpListPtr){
                registerTypeRef(registerTypeRef, tp.constValueType);
                for(const TypeRef* const bound : tp.interfaceBounds)
                    registerTypeRef(registerTypeRef, bound);
            }
        }
        for(const auto& methodPtr : m_polymorphicMethods){
            if(!methodPtr)
                continue;
            registerTypeRef(registerTypeRef, methodPtr->returnType);
            for(const ParamDecl& p : methodPtr->params)
                registerTypeRef(registerTypeRef, p.type);
        }

        HashMap<const TemplateParamDecl*, i32, MetaArena> tplListByFirstPtr(scratchArena);
        for(usize li = 0u; li < m_ownedTemplateParamLists.size(); ++li){
            const MVector<TemplateParamDecl>& list = *m_ownedTemplateParamLists[li];
            if(!list.empty())
                tplListByFirstPtr.emplace(list.data(), static_cast<i32>(li));
        }

        writer.writeValue(static_cast<u32>(flatTypeRefs.size()));
        for(const TypeRef* const tr : flatTypeRefs){
            writer.writeString(tr->name);
            writer.writeValue(static_cast<u8>(tr->refMode));
            writer.writeValue(static_cast<u64>(tr->inferredCaptureBytes));
            writer.writeValue(tr->span);
            writer.writeBool(tr->isFuncType);
            writer.writeBool(tr->isAuto);
            writer.writeValue(static_cast<u32>(tr->templateArgs.size()));
            const i32 retIdx = tr->funcReturnType ? typeRefIndices.find(tr->funcReturnType)->second : -1;
            writer.writeValue(retIdx);
            writer.writeValue(static_cast<u32>(tr->funcParams.size()));
            for(const TypeArg& arg : tr->templateArgs){
                const i32 argIdx = arg.type ? typeRefIndices.find(arg.type)->second : -1;
                writer.writeValue(argIdx);
                writer.writeString(arg.constantText);
            }
            for(const FuncParamType& fp : tr->funcParams){
                writer.writeValue(static_cast<u8>(fp.mode));
                const i32 ptIdx = fp.type ? typeRefIndices.find(fp.type)->second : -1;
                writer.writeValue(ptIdx);
            }
        }

        writer.writeValue(static_cast<u32>(m_ownedTemplateParamLists.size()));
        for(const auto& tpListPtr : m_ownedTemplateParamLists){
            const MVector<TemplateParamDecl>& list = *tpListPtr;
            writer.writeValue(static_cast<u32>(list.size()));
            for(const TemplateParamDecl& tp : list){
                writer.writeString(tp.name);
                const i32 cvIdx = tp.constValueType ? typeRefIndices.find(tp.constValueType)->second : -1;
                writer.writeValue(cvIdx);
                writer.writeValue(tp.span);
                writer.writeValue(static_cast<u32>(tp.interfaceBounds.size()));
                for(const TypeRef* const bound : tp.interfaceBounds){
                    const i32 bIdx = bound ? typeRefIndices.find(bound)->second : -1;
                    writer.writeValue(bIdx);
                }
            }
        }

        writer.writeValue(static_cast<u32>(m_objectTypes.size()));
        for(const BuiltinObjectTypeInfo& obj : m_objectTypes){
            writer.writeString(obj.adhdName);
            writer.writeString(obj.cppExportName);
            writer.writeString(obj.requiredHeader);
            writer.writeValue(obj.byteSize);
            writer.writeValue(obj.byteAlign);
            writer.writeBool(obj.isArenaBacked);
            writer.writeBool(obj.isSelfDefaulted);
            writer.writeBool(obj.isSmallValue);
            writer.writeBool(obj.isBorrowView);
            writer.writeBool(obj.isBinarySerializable);
            writer.writeBool(obj.isSingleValueCtor);
            writer.writeBool(obj.emitsBraceFieldInit);
            writer.writeBool(obj.isStringLike);
            writer.writeBool(obj.isStringSwitch);
            writer.writeBool(obj.isNonThreadSafeContainer);
            writer.writeBool(obj.isNwbMapKey);
            writer.writeBool(obj.requiresTCharAdaptation);
            writer.writeValue(obj.arenaCtorMode);
            writer.writeBool(obj.isCopyMoveConstructible);
            writer.writeBool(obj.isAssignable);
            writer.writeBool(obj.isAbstract);
            writer.writeBool(obj.isPolymorphicBase);
            writer.writeValue(obj.formatArgWrapKind);
            i32 tplListIdx = -1;
            if(!obj.templateParams.empty()){
                const auto it = tplListByFirstPtr.find(obj.templateParams.data());
                if(it != tplListByFirstPtr.end())
                    tplListIdx = it->second;
            }
            writer.writeValue(tplListIdx);
        }

        writer.writeValue(static_cast<u32>(m_enums.size()));
        for(const BuiltinEnumInfo& en : m_enums){
            writer.writeString(en.adhdName);
            writer.writeString(en.cppTypeName);
            writer.writeString(en.cppScopeName);
            writer.writeString(en.requiredHeader);
            writer.writeValue(en.byteSize);
            writer.writeValue(en.byteAlign);
            writer.writeBool(en.isFlag);
        }

        writer.writeValue(static_cast<u32>(m_enumEntries.size()));
        for(const BuiltinEnumEntryInfo& entry : m_enumEntries){
            writer.writeString(entry.adhdEnumName);
            writer.writeString(entry.entryName);
        }

        writer.writeValue(static_cast<u32>(m_fields.size()));
        for(const BuiltinFieldInfo& f : m_fields){
            writer.writeString(f.receiverTypeName);
            writer.writeString(f.fieldName);
            writer.writeString(f.fieldTypeName);
            writer.writeString(f.elemTypeName);
            writer.writeString(f.countText);
        }

        writer.writeValue(static_cast<u32>(m_receiverMethods.size()));
        for(const BuiltinReceiverMethodInfo& rm : m_receiverMethods){
            writer.writeString(rm.receiverTypeName);
            writer.writeString(rm.methodName);
            writer.writeString(rm.returnTypeName);
            writer.writeString(rm.returnTypeArgName);
            writer.writeString(rm.returnNestedTypeArgName);
            writer.writeString(rm.returnErrorTypeName);
            writer.writeBool(rm.synthesizeTypeRef);
            writer.writeBool(rm.isMutating);
            writer.writeBool(rm.returnsReference);
            writer.writeValue(static_cast<u8>(rm.returnTypeArgRefMode));
        }

        writer.writeValue(static_cast<u32>(m_receiverSignatures.size()));
        for(const BuiltinReceiverSignatureInfo& sig : m_receiverSignatures){
            writer.writeString(sig.receiverTypeName);
            writer.writeString(sig.methodName);
            for(const BuiltinFunctionArgRule::Enum r : sig.argRules)
                writer.writeValue(static_cast<u16>(r));
            writer.writeValue(sig.arity);
            writer.writeValue(sig.retainedArgMask);
            writer.writeValue(sig.cppConstReferenceMask);
            writer.writeBool(sig.allowsCppUnwind);
        }

        writer.writeValue(static_cast<u32>(m_argRules.size()));
        for(const BuiltinFunctionArgRuleInfo& r : m_argRules){
            writer.writeValue(static_cast<u16>(r.rule));
            writer.writeString(r.expectedParamType);
            writer.writeString(r.requiredExactType);
            writer.writeString(r.errorMessage);
            writer.writeBool(r.isMut);
            writer.writeBool(r.requiresStringLike);
            writer.writeBool(r.requiresIntegerScalar);
        }

        writer.writeValue(static_cast<u32>(m_visitorRules.size()));
        for(const BuiltinVisitorRuleInfo& vr : m_visitorRules){
            writer.writeValue(static_cast<u16>(vr.rule));
            writer.writeString(vr.valueType);
            writer.writeString(vr.keyType);
            writer.writeBool(vr.mutableWithReceiver);
        }

        writer.writeValue(static_cast<u32>(m_functions.size()));
        for(const BuiltinFunctionInfo& fn : m_functions){
            writer.writeString(fn.adhdName);
            writer.writeString(fn.cppExportName);
            writer.writeString(fn.returnTypeName);
            writer.writeString(fn.returnTypeArgName);
            writer.writeString(fn.returnNestedTypeArgName);
            writer.writeString(fn.returnErrorTypeName);
            writer.writeString(fn.requiredHeader);
            writer.writeValue(static_cast<u64>(fn.minArity));
            writer.writeValue(static_cast<u64>(fn.arity));
            writer.writeValue(static_cast<u8>(fn.returnRule));
            writer.writeValue(static_cast<u8>(fn.callLowering));
            writer.writeBool(fn.prependsScratchArena);
            writer.writeBool(fn.isVariadic);
            writer.writeBool(fn.wrapsFormatArgs);
            writer.writeBool(fn.requiresExplicitTypeArg);
            writer.writeBool(fn.returnsBorrowedView);
            writer.writeBool(fn.rejectsSimdStorage);
            writer.writeBool(fn.returnsReference);
            for(const BuiltinFunctionArgRule::Enum r : fn.argRules)
                writer.writeValue(static_cast<u16>(r));
            writer.writeValue(fn.retainedArgMask);
            writer.writeValue(fn.explicitTypeParamMask);
        }

        writer.writeValue(static_cast<u32>(m_globalConstants.size()));
        for(const BuiltinGlobalConstantInfo& gc : m_globalConstants){
            writer.writeString(gc.adhdName);
            writer.writeString(gc.cppExportName);
            writer.writeString(gc.adhdTypeName);
            writer.writeString(gc.requiredHeader);
        }

        HashMap<const MethodDecl*, u32, MetaArena> polyMethodToIndex(scratchArena);
        writer.writeValue(static_cast<u32>(m_polymorphicMethods.size()));
        for(usize mi = 0u; mi < m_polymorphicMethods.size(); ++mi){
            const MethodDecl* const method = m_polymorphicMethods[mi].get();
            if(method)
                polyMethodToIndex.emplace(method, static_cast<u32>(mi));
            writer.writeString(method ? method->name : MStringView{});
            writer.writeBool(method ? method->isConst : false);
            writer.writeBool(method ? method->isVariadic : false);
            writer.writeBool(method ? method->allowsCppUnwind : false);
            const i32 retTypeIdx = (method && method->returnType) ? typeRefIndices.find(method->returnType)->second : -1;
            writer.writeValue(retTypeIdx);
            const u32 paramCount = method ? static_cast<u32>(method->params.size()) : 0u;
            writer.writeValue(paramCount);
            if(method){
                for(const ParamDecl& param : method->params){
                    writer.writeValue(static_cast<u8>(param.mode));
                    writer.writeString(param.name);
                    const i32 pTypeIdx = param.type ? typeRefIndices.find(param.type)->second : -1;
                    writer.writeValue(pTypeIdx);
                    writer.writeBool(param.cppConstReference);
                }
            }
        }

        writer.writeValue(static_cast<u32>(m_polymorphicDecls.size()));
        for(const auto& clsPtr : m_polymorphicDecls){
            const ClassDecl* const cls = clsPtr.get();
            writer.writeString(cls ? cls->name : MStringView{});
            writer.writeBool(cls ? cls->isExtern : false);
            writer.writeBool(cls ? cls->isPolymorphicBase : false);
            const u32 methodCount = cls ? static_cast<u32>(cls->methods.size()) : 0u;
            writer.writeValue(methodCount);
            if(cls){
                for(const MethodDecl* const m : cls->methods){
                    const auto it = polyMethodToIndex.find(m);
                    writer.writeValue(it != polyMethodToIndex.end() ? it->second : 0u);
                }
            }
        }

        PreludeBinaryWriter headerWriter(scratchArena);
        headerWriter.writeValue(s_BinaryCacheMagic);
        headerWriter.writeValue(s_BinaryCacheVersion);
        headerWriter.writeValue(ComputeBinaryCacheSchemaSignature());
        headerWriter.writeValue(static_cast<u32>(watchedList.files.size()));
        for(const WatchedPreludeFile& wf : watchedList.files){
            headerWriter.writeRawString(MStringView(wf.relativePath.data(), wf.relativePath.size()));
            headerWriter.writeValue(wf.fileSize);
            headerWriter.writeValue(wf.mtimeTicks);
            headerWriter.writeBool(wf.fromProjectRoot);
        }
        const MString& blob = writer.stringBlob();
        headerWriter.writeValue(static_cast<u32>(blob.size()));

        const Vector<u8, MetaArena>& headerBytes = headerWriter.payloadBytes();
        const Vector<u8, MetaArena>& payloadBytes = writer.payloadBytes();
        Vector<u8, MetaArena> fullImage(scratchArena);
        fullImage.reserve(headerBytes.size() + blob.size() + payloadBytes.size());
        fullImage.insert(fullImage.end(), headerBytes.begin(), headerBytes.end());
        if(!blob.empty()){
            const u8* blobBytes = reinterpret_cast<const u8*>(blob.data());
            fullImage.insert(fullImage.end(), blobBytes, blobBytes + blob.size());
        }
        fullImage.insert(fullImage.end(), payloadBytes.begin(), payloadBytes.end());

        const ::Path<MetaArena> cachePath = ResolvePreludeCachePath(scratchArena);
        static Atomic<u64> s_TmpSeq{ 0u };
        const u64 seq = s_TmpSeq.fetch_add(1u, MemoryOrder::relaxed);
#if defined(FOUNDATION_PLATFORM_WINDOWS)
        const u64 pid = static_cast<u64>(GetCurrentProcessId());
        const u64 tid = static_cast<u64>(GetCurrentThreadId());
#else
        const u64 pid = static_cast<u64>(::getpid());
        const u64 tid = 0u;
#endif
        const MString tmpName = StringFormat(scratchArena, "{}.tmp.{}.{}.{}", PathToGenericString<MChar>(scratchArena, cachePath), pid, tid, seq);
        const ::Path<MetaArena> tmpPath(scratchArena, MStringView(tmpName.data(), tmpName.size()));
        if(!WriteBinaryFile(tmpPath, fullImage)){
            if(const auto removed = RemoveFile(tmpPath); !removed || !*removed)
                return;
            return;
        }
        if(!RenamePath(tmpPath, cachePath)){
            if(const auto removed = RemoveFile(tmpPath); !removed || !*removed)
                return;
        }
    }

    void load(){
        MetaArena parseArena(MetascriptArenaScope::s_ParserScratch);
        if(tryLoadBinaryCache(parseArena))
            return;

        Alloc::ScratchArena scratchArena(MetascriptArenaScope::s_ParserScratch);
        WatchedPreludeList watchedList(parseArena);

        MString rawGlobalSource(parseArena);
        const b8 loadedGlobal = tryReadProjectFile(ContractSemantics::s_GlobalExternPreludePath, parseArena, rawGlobalSource, &watchedList);
        MString rawCoreSource(parseArena);
        const b8 loadedCore = tryReadProjectFile(ContractSemantics::s_CoreExternPreludePath, parseArena, rawCoreSource, &watchedList);
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
        PreludeIncludeResolver includeResolver(*this, &watchedList);
        Preprocessor preprocessor(parseArena, scratchArena, errors, &includeResolver, false);
        m_sourceText.clear();
        const b8 preprocessed = preprocessor.process(MStringView(rawPreludeSource.data(), rawPreludeSource.size()), m_sourceText);
        if(!preprocessed || !errors.empty() || m_sourceText.empty()){
            recordLoadError({}, errors, "preprocessing yielded no declarations");
            return;
        }

        const MStringView activeSource(m_sourceText.data(), m_sourceText.size());
        TranslationUnit parseUnit(parseArena);
        ParserCursor cursor(activeSource, parseArena, scratchArena, errors);
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
        saveBinaryCache(watchedList, parseArena);
    }


private:
    MetaArena m_arena;
    MString m_sourceText;
    MString m_loadError;
    MVector<GlobalUniquePtr<MString>> m_ownedStrings;
    MVector<GlobalUniquePtr<TypeRef>> m_ownedTypeRefs;
    MVector<GlobalUniquePtr<MVector<TemplateParamDecl>>> m_ownedTemplateParamLists;
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
    HashMap<MStringView, usize, MetaArena, MStringViewHash> m_objectTypeByName;
    HashMap<MStringView, usize, MetaArena, MStringViewHash> m_enumByName;
    HashSet<StringViewPair, MetaArena, StringViewPairHash> m_enumEntryByKey;
    HashMap<StringViewPair, usize, MetaArena, StringViewPairHash> m_fieldByKey;
    HashMap<StringViewPair, usize, MetaArena, StringViewPairHash> m_receiverMethodByKey;
    HashMap<StringViewPair, usize, MetaArena, StringViewPairHash> m_receiverSigByKey;
    HashMap<MStringView, usize, MetaArena, MStringViewHash> m_functionByName;
    HashMap<StringViewPair, usize, MetaArena, StringViewPairHash> m_scopedFunctionByKey;
    HashMap<MStringView, usize, MetaArena, MStringViewHash> m_globalConstantByName;
    HashMap<StringViewPair, usize, MetaArena, StringViewPairHash> m_scopedConstantByKey;
    MStringMap<u8> m_simdFieldTypes;
    MStringMap<BuiltinFunctionArgRule::Enum> m_exactRuleByKey;
    MStringMap<BuiltinFunctionArgRule::Enum> m_visitorRuleByKey;
    MVector<GlobalUniquePtr<ClassDecl>> m_polymorphicDecls;
    MVector<GlobalUniquePtr<MethodDecl>> m_polymorphicMethods;
    HashMap<MStringView, usize, MetaArena, MStringViewHash> m_polymorphicDeclByName;
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
    if(const BuiltinObjectTypeInfo* info = __hidden_adhd_type_utils::GetStaticBuiltinTableIndex().findObjectType(name))
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
    return __hidden_adhd_type_utils::GetStaticBuiltinTableIndex().findGenericType(name);
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
    return __hidden_adhd_type_utils::GetStaticBuiltinTableIndex().findUnalignedVectorType(name);
}

const BuiltinSimdVectorInfo* FindBuiltinSimdVectorType(const MStringView name)noexcept{
    return __hidden_adhd_type_utils::GetStaticBuiltinTableIndex().findSimdVectorType(name);
}

const BuiltinSimdMatrixInfo* FindBuiltinSimdMatrixType(const MStringView name)noexcept{
    return __hidden_adhd_type_utils::GetStaticBuiltinTableIndex().findSimdMatrixType(name);
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
    return __hidden_adhd_type_utils::GetStaticBuiltinTableIndex().findMemberMethod(name);
}

const BuiltinReceiverMethodInfo* FindBuiltinReceiverMethod(const MStringView receiverTypeName, const MStringView methodName)noexcept{
    if(const BuiltinReceiverMethodInfo* info = __hidden_adhd_type_utils::GetStaticBuiltinTableIndex().findReceiverMethod(receiverTypeName, methodName))
        return info;
    return __hidden_adhd_type_utils::GetCoreExternRegistry().findReceiverMethod(receiverTypeName, methodName);
}

const BuiltinReceiverSignatureInfo* FindBuiltinReceiverSignature(const MStringView receiverTypeName, const MStringView methodName)noexcept{
    return __hidden_adhd_type_utils::GetCoreExternRegistry().findReceiverSignature(receiverTypeName, methodName);
}

const ClassDecl* FindBuiltinPolymorphicInterfaceDecl(const MStringView adhdName)noexcept{
    return __hidden_adhd_type_utils::GetCoreExternRegistry().findPolymorphicInterface(adhdName);
}

const BuiltinFieldInfo* FindBuiltinField(const MStringView receiverTypeName, const MStringView fieldName)noexcept{
    if(const BuiltinFieldInfo* info = __hidden_adhd_type_utils::GetStaticBuiltinTableIndex().findField(receiverTypeName, fieldName))
        return info;
    return __hidden_adhd_type_utils::GetCoreExternRegistry().findField(receiverTypeName, fieldName);
}

const BuiltinFunctionArgRuleInfo* FindBuiltinFunctionArgRule(const BuiltinFunctionArgRule::Enum rule)noexcept{
    const usize idx = static_cast<usize>(rule);
    if(idx < static_cast<usize>(BuiltinFunctionArgRule::StaticCount))
        return &__hidden_adhd_type_utils::s_BuiltinFunctionArgRules[idx];
    return __hidden_adhd_type_utils::GetCoreExternRegistry().findArgRule(rule);
}

const BuiltinVisitorRuleInfo* FindBuiltinVisitorRule(const BuiltinFunctionArgRule::Enum rule)noexcept{
    return __hidden_adhd_type_utils::GetCoreExternRegistry().findVisitorRule(rule);
}

const BuiltinFunctionInfo* FindBuiltinFunction(const MStringView name)noexcept{
    if(const BuiltinFunctionInfo* info = __hidden_adhd_type_utils::GetStaticBuiltinTableIndex().findFunction(name))
        return info;
    return __hidden_adhd_type_utils::GetCoreExternRegistry().findFunction(name);
}

const BuiltinFunctionInfo* FindBuiltinScopedFunction(const MStringView scopeTypeName, const MStringView memberName)noexcept{
    if(!memberName.empty()){
        const MStringView prefix = __hidden_adhd_type_utils::ResolveBuiltinScopePrefix(scopeTypeName);
        if(!prefix.empty()){
            if(const BuiltinFunctionInfo* info = __hidden_adhd_type_utils::GetStaticBuiltinTableIndex().findScopedFunction(prefix, memberName))
                return info;
        }
    }
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
    if(__hidden_adhd_type_utils::GetStaticBuiltinTableIndex().hasSimdRegisterField(typeName))
        return true;
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
    return __hidden_adhd_type_utils::GetStaticBuiltinTableIndex().findSpecialFunctionKind(name);
}

SimdLaneZeroRuleKind::Enum FindSimdLaneZeroRule(const MStringView name)noexcept{
    return __hidden_adhd_type_utils::GetStaticBuiltinTableIndex().findSimdLaneZeroRule(name);
}

SpecialMethodKind::Enum FindSpecialMethodSemantics(const MStringView methodName)noexcept{
    return __hidden_adhd_type_utils::GetStaticBuiltinTableIndex().findSpecialMethodKind(methodName);
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

