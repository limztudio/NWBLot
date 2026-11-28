// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "const_eval.h"
#include "../ast/keyword_semantics.h"
#include "../ast/builtin_type_semantics.h"

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_InferredLambdaCaptureBytes = 64u;

namespace GenericParamKind{
    enum Enum : u8{
        SingleType,
        TwoType,
        OneOrTwoType,
        TypeAndCount,
    };
};

namespace InlineLayoutKind{
    enum Enum : u8{
        None = 0u,
        Atomic,
        Optional,
        Expected,
        Array,
        FixedVector,
    };
};

namespace ReflectedLinearKind{
    enum Enum : u8{
        None = 0u,
        DynamicBack,
        ForwardList,
        Set,
        FixedVector,
        FixedArray,
    };
};

namespace BuiltinMemberReturnRule{
    enum Enum : u8{
        None = 0u,
        FixedType,
        ReceiverType,
        FirstTypeArg,
        SecondTypeArgOrDefault,
    };
};

namespace BuiltinFunctionReturnRule{
    enum Enum : u8{
        FixedType,
        PromoteSimd,
        LaneScalar,
        MatrixMultiply,
        Unexpected,
        CallTemplateArg0,
        CallTemplateArg0OrFixedType,
    };
};

namespace BuiltinFunctionArgRule{
    enum Enum : u16{
#define NWB_ADHD_EXPORT_FUNCTION_ARG_RULE(ruleName, expectedParamType, isMut, requiredExactType, requiresStringLike, requiresIntegerScalar, errorMessage) \
        ruleName,
#include "../export_functions.h"
#undef NWB_ADHD_EXPORT_FUNCTION_ARG_RULE
        StaticCount,
    };
};

namespace SpecialFunctionKind{
    enum Enum : u8{
        None = 0u,
        Lerp,
        Rsqrt,
        IsFinite,
        Mul,
        Transpose,
    };
};

namespace SimdLaneZeroRuleKind{
    enum Enum : u8{
        None = 0u,
        Preserve2Or3LanesFromArg0,
        AtLeast3LanesFromTwoArgs,
        UnitZeroClampFromArg0,
    };
};

namespace SpecialMethodKind{
    enum Enum : u8{
        None = 0u,
        Substr,
        CheckedElementAccess,
        UnwrapValue,
        Deref,
        EcsEmplaceSystem,
        EcsEmplaceComponent,
        AssetLoadBinary,
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct BuiltinObjectTypeInfo{
    MStringView adhdName;
    MStringView cppExportName;
    MStringView requiredHeader;
    u32 byteSize;
    u32 byteAlign;
    b8 isArenaBacked;
    b8 isSelfDefaulted;
    b8 isNothrowDefaultConstructible;
    b8 isSmallValue;
    b8 isBorrowView;
    b8 isBinarySerializable;
    b8 isSingleValueCtor;
    b8 emitsBraceFieldInit;
    b8 isStringLike;
    b8 isStringSwitch;
    b8 isNonThreadSafeContainer;
    b8 isNwbMapKey;
    b8 requiresTCharAdaptation;
    u8 arenaCtorMode;
    b8 isCopyMoveConstructible;
    b8 isAssignable;
    b8 isAbstract;
    b8 isPolymorphicBase;
    b8 isEmptyBase;
    u8 formatArgWrapKind;
    Span<const TemplateParamDecl> templateParams{};
};

struct BuiltinEnumInfo{
    MStringView adhdName;
    MStringView cppTypeName;
    MStringView cppScopeName;
    MStringView requiredHeader;
    u32 byteSize;
    u32 byteAlign;
    b8 isFlag;
};

struct BuiltinEnumEntryInfo{
    MStringView adhdEnumName;
    MStringView entryName;
};

struct BuiltinGenericTypeInfo{
    MStringView adhdName;
    MStringView cppExportName;
    MStringView requiredHeader;
    GenericParamKind::Enum paramKind;
    InlineLayoutKind::Enum inlineLayoutKind;
    ReflectedLinearKind::Enum reflectedLinearKind;
    u32 byteSize;
    u32 byteAlign;
    b8 isArenaBacked;
    b8 isSelfDefaulted;
    b8 isSmallValue;
    b8 isBorrowView;
    b8 isBinarySerializable;
    b8 isSingleValueCtor;
    b8 emitsBraceFieldInit;
    b8 isNonThreadSafeContainer;
    b8 isNonRandomAccessContainer;
    b8 isIndexableSequence;
    b8 isReflectedMap;
    b8 isAssetHandle;
    b8 isCopyMoveConstructible;
    b8 emitsFirstTypeArgOnlyInCpp;
    b8 arenaFirstInCtor;
};

struct BuiltinBinaryHashTypeInfo{
    MStringView adhdName;
    MStringView memberSuffix;
};

struct BuiltinNwbValueTypeInfo{
    MStringView adhdName;
    MStringView nwbCheckMethod;
    MStringView nwbAccessorMethod;
    MStringView valuePrefix;
    MStringView valueSuffix;
    MStringView directFieldPrefix;
    MStringView directFieldSuffix;
    MStringView optionalFieldPrefix;
    MStringView optionalFieldSuffix;
    b8 requiresNameAdmission;
};

struct BuiltinImplicitConversionInfo{
    MStringView fromTypeName;
    MStringView toTypeName;
    b8 propagatesFirstTypeArg;
};

struct BuiltinUnalignedVectorInfo{
    MStringView adhdName;
    MStringView cppExportName;
    MStringView laneScalarType;
    u32 lanes;
    u32 altCtorArity;
    u32 byteSize;
    u32 byteAlign;
    b8 isHalfFloat;
};

struct BuiltinSimdVectorInfo{
    MStringView adhdName;
    MStringView cppExportName;
    MStringView xyzScalarType;
    MStringView wScalarType;
    MStringView regCppType;
    MStringView loadCppFunc;
    MStringView storeCppFunc;
    u32 lanes;
    u32 altCtorArity;
    u32 byteSize;
    u32 byteAlign;
    b8 isFloatReg;
    b8 isDoubleReg;
    b8 isIntReg;
    b8 isSignedInt;
    b8 isMixed;
};

struct BuiltinSimdMatrixInfo{
    MStringView adhdName;
    MStringView cppExportName;
    MStringView elemScalarType;
    MStringView rowVectorType;
    MStringView regCppType;
    MStringView loadCppFunc;
    MStringView storeCppFunc;
    u32 elemCount;
    u32 byteSize;
    u32 byteAlign;
    b8 isDoubleReg;
};

struct BuiltinMemberMethodInfo{
    MStringView methodName;
    MStringView returnTypeName;
    BuiltinMemberReturnRule::Enum returnRule;
    b8 isMutating;
    b8 firstArgIsMut;
    b8 adaptsFirstArgToElement;
    b8 adaptsSecondArgToElement;
    b8 allowsCppUnwind;
};

struct BuiltinReceiverMethodInfo{
    MStringView receiverTypeName;
    MStringView methodName;
    MStringView returnTypeName;
    MStringView returnTypeArgName;
    MStringView returnNestedTypeArgName;
    MStringView returnErrorTypeName;
    b8 synthesizeTypeRef;
    b8 isMutating;
    b8 returnsReference;
    ParamPassingMode::Enum returnTypeArgRefMode = ParamPassingMode::None;
    b8 allowsCppUnwind = false;
};

struct BuiltinReceiverSignatureInfo{
    MStringView receiverTypeName;
    MStringView methodName;
    Array<BuiltinFunctionArgRule::Enum, 9> argRules;
    u8 arity;
    u32 retainedArgMask = 0u;
    u32 cppConstReferenceMask = 0u;
    b8 allowsCppUnwind = false;
};

struct BuiltinVisitorRuleInfo{
    BuiltinFunctionArgRule::Enum rule;
    MStringView valueType;
    MStringView keyType;
    b8 mutableWithReceiver;
};

struct BuiltinFieldInfo{
    MStringView receiverTypeName;
    MStringView fieldName;
    MStringView fieldTypeName;
    MStringView elemTypeName;
    MStringView countText;
};

struct BuiltinFunctionArgRuleInfo{
    BuiltinFunctionArgRule::Enum rule;
    MStringView expectedParamType;
    MStringView requiredExactType;
    MStringView errorMessage;
    b8 isMut;
    b8 requiresStringLike;
    b8 requiresIntegerScalar;
};

struct BuiltinFunctionInfo{
    MStringView adhdName;
    MStringView cppExportName;
    MStringView returnTypeName;
    MStringView returnTypeArgName;
    MStringView returnNestedTypeArgName;
    MStringView returnErrorTypeName;
    MStringView requiredHeader;
    usize minArity;
    usize arity;
    BuiltinFunctionReturnRule::Enum returnRule;
    b8 prependsScratchArena;
    b8 isVariadic;
    b8 wrapsFormatArgs;
    b8 requiresExplicitTypeArg;
    b8 returnsBorrowedView;
    b8 rejectsSimdStorage;
    b8 returnsReference;
    b8 allowsCppUnwind = false;
    Array<BuiltinFunctionArgRule::Enum, 20> argRules;
    u32 retainedArgMask = 0u;
    u32 explicitTypeParamMask = 0u;
    u32 byteViewArgMask = 0u;
    u32 platformLiteralArgMask = 0u;
};

struct BuiltinGlobalConstantInfo{
    MStringView adhdName;
    MStringView cppExportName;
    MStringView adhdTypeName;
    MStringView requiredHeader;
};

struct BuiltinFormatArgWrapInfo{
    u8 wrapKindId;
    b8 wrapInDiagnosticFormat;
    MStringView cppPrefix;
    MStringView cppSuffix;
};

struct BuiltinContainerArgConstraintInfo{
    BuiltinFunctionArgRule::Enum rule;
    MStringView elementTypeName;
    usize templateArgCount;
    b8 isExactElementCheck;
    b8 checksContiguousSequence;
    MStringView countText;
};

struct BuiltinSpecialFunctionInfo{
    MStringView adhdName;
    SpecialFunctionKind::Enum kind;
};

struct BuiltinSimdLaneZeroRuleInfo{
    MStringView adhdName;
    SimdLaneZeroRuleKind::Enum ruleKind;
};

struct BuiltinSpecialMethodInfo{
    MStringView methodName;
    SpecialMethodKind::Enum kind;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] constexpr b8 IsFieldReflected(const FieldDecl& field)noexcept{
    return !field.isTransient && (field.access == AccessSpecifier::Public || field.isReflect);
}

[[nodiscard]] constexpr b8 IsMethodReflected(const MethodDecl& method)noexcept{
    return !method.isConstructor && !method.isDestructor && (method.access == AccessSpecifier::Public || method.isReflect);
}

[[nodiscard]] constexpr b8 IsEnumEntryReflected(const EnumEntry& entry)noexcept{
    return KeywordSemantics::IsEnumSentryReflected(entry.name);
}

[[nodiscard]] MStringView BuiltinPreludeLoadError()noexcept;
[[nodiscard]] const BuiltinObjectTypeInfo* FindStaticBuiltinObjectType(MStringView name)noexcept;
[[nodiscard]] const BuiltinObjectTypeInfo* FindBuiltinObjectType(MStringView name)noexcept;
[[nodiscard]] const BuiltinEnumInfo* FindBuiltinEnum(MStringView name)noexcept;
[[nodiscard]] b8 HasBuiltinEnumEntry(MStringView enumName, MStringView entryName)noexcept;
[[nodiscard]] b8 IsArenaFirstCtorType(MStringView name)noexcept;
[[nodiscard]] b8 OmitsArenaInCtorType(MStringView name)noexcept;
[[nodiscard]] const BuiltinGenericTypeInfo* FindBuiltinGenericType(MStringView name)noexcept;
[[nodiscard]] b8 FindBuiltinBinaryHashMemberSuffix(MStringView name, MStringView& outMemberSuffix)noexcept;
[[nodiscard]] const BuiltinNwbValueTypeInfo* FindBuiltinNwbValueType(MStringView name)noexcept;
[[nodiscard]] const BuiltinUnalignedVectorInfo* FindBuiltinUnalignedVectorType(MStringView name)noexcept;
[[nodiscard]] const BuiltinSimdVectorInfo* FindBuiltinSimdVectorType(MStringView name)noexcept;
[[nodiscard]] const BuiltinSimdMatrixInfo* FindBuiltinSimdMatrixType(MStringView name)noexcept;
[[nodiscard]] b8 IsValidBuiltinVectorOrMatrixCtorArity(MStringView baseTypeName, usize argCount)noexcept;
[[nodiscard]] b8 FindBuiltinFixedLayout(MStringView name, usize& outSize, usize& outAlign)noexcept;
[[nodiscard]] const BuiltinMemberMethodInfo* FindBuiltinMemberMethod(MStringView name)noexcept;
[[nodiscard]] const BuiltinReceiverMethodInfo* FindBuiltinReceiverMethod(MStringView receiverTypeName, MStringView methodName)noexcept;
[[nodiscard]] const BuiltinReceiverSignatureInfo* FindBuiltinReceiverSignature(MStringView receiverTypeName, MStringView methodName)noexcept;
[[nodiscard]] const ClassDecl* FindBuiltinPolymorphicInterfaceDecl(MStringView adhdName)noexcept;
[[nodiscard]] const BuiltinFieldInfo* FindBuiltinField(MStringView receiverTypeName, MStringView fieldName)noexcept;
[[nodiscard]] const BuiltinFunctionArgRuleInfo* FindBuiltinFunctionArgRule(BuiltinFunctionArgRule::Enum rule)noexcept;
[[nodiscard]] const BuiltinVisitorRuleInfo* FindBuiltinVisitorRule(BuiltinFunctionArgRule::Enum rule)noexcept;
[[nodiscard]] const BuiltinFunctionInfo* FindBuiltinFunction(MStringView name)noexcept;
[[nodiscard]] const BuiltinFunctionInfo* FindBuiltinScopedFunction(MStringView scopeTypeName, MStringView memberName)noexcept;
[[nodiscard]] const BuiltinGlobalConstantInfo* FindBuiltinGlobalConstant(MStringView name)noexcept;
[[nodiscard]] const BuiltinGlobalConstantInfo* FindBuiltinScopedConstant(MStringView scopeTypeName, MStringView memberName)noexcept;
[[nodiscard]] b8 IsBorrowViewType(MStringView name)noexcept;
[[nodiscard]] b8 IsNonSerializableBuiltinType(MStringView name)noexcept;
[[nodiscard]] b8 IsSingleValueCtorBuiltinType(MStringView name)noexcept;
[[nodiscard]] b8 NeedsBraceDefaultFieldInit(MStringView name)noexcept;
[[nodiscard]] b8 IsStringLikeType(MStringView name)noexcept;
[[nodiscard]] b8 IsOwningStringType(MStringView name)noexcept;
[[nodiscard]] b8 HasMutableStringElements(MStringView name)noexcept;
[[nodiscard]] b8 IsStringByteAccessorMethod(MStringView receiverTypeName, MStringView methodName)noexcept;
[[nodiscard]] b8 IsStringReceiverBorrowViewMethod(MStringView receiverTypeName, MStringView methodName)noexcept;
[[nodiscard]] b8 IsBuiltinStringViewBorrowFunction(MStringView name)noexcept;
[[nodiscard]] b8 IsContainerOrWrapperValueAccessMethod(MStringView receiverTypeName, MStringView methodName, b8 includeStrings)noexcept;
[[nodiscard]] b8 IsNwbMapKeyType(MStringView name)noexcept;
[[nodiscard]] b8 RequiresTCharAdaptation(MStringView name)noexcept;
[[nodiscard]] b8 IsImplicitlyConvertibleBuiltinType(MStringView fromTypeName, MStringView toTypeName)noexcept;
[[nodiscard]] b8 DoesImplicitConversionPropagateFirstTypeArg(MStringView fromTypeName, MStringView toTypeName)noexcept;

[[nodiscard]] b8 IsNonThreadSafeContainer(MStringView typeName)noexcept;
[[nodiscard]] b8 IsFixedArrayContainerType(MStringView name)noexcept;
[[nodiscard]] b8 IsOptionalContainerType(MStringView name)noexcept;
[[nodiscard]] b8 IsOptionalOrExpectedGenericType(MStringView name)noexcept;
[[nodiscard]] b8 IsAtomicGenericType(MStringView name)noexcept;
[[nodiscard]] b8 IsAssetHandleType(MStringView name)noexcept;
[[nodiscard]] b8 CheckSwizzlePattern(MStringView text, u32 maxLanes, b8& outIsPattern, b8& outValidLanes)noexcept;

[[nodiscard]] b8 IsVoidTypeRef(const TypeRef* ref)noexcept;

[[nodiscard]] i32 ScoreCallableParams(const SymbolTable& symbols, Span<const ParamDecl> params, Span<const MStringView> argTypeNames, Span<const u8> argIsMut)noexcept;

[[nodiscard]] b8 IsReflectedLinearContainerType(MStringView baseName)noexcept;
[[nodiscard]] b8 IsReflectedMapContainerType(MStringView baseName)noexcept;
[[nodiscard]] b8 IsNonCopyableBaseInterfaceName(MStringView name)noexcept;
[[nodiscard]] b8 IsMoveOnlyBaseInterfaceName(MStringView name)noexcept;
[[nodiscard]] MStringView FindNonCopyableBaseInterfaceCppName(MStringView name)noexcept;
[[nodiscard]] b8 IsMoveOnlyTypeName(MStringView name)noexcept;
[[nodiscard]] b8 HasBuiltinSimdRegisterField(MStringView typeName)noexcept;
[[nodiscard]] b8 IsOverAlignedBuiltinType(MStringView typeName)noexcept;
[[nodiscard]] b8 IsStringBorrowViewType(MStringView name)noexcept;
[[nodiscard]] b8 AllowsInterfaceTypeArg(MStringView name)noexcept;
[[nodiscard]] b8 IsSmartPointerDerefType(MStringView name)noexcept;
[[nodiscard]] b8 IsSubsystemVectorContainerType(MStringView name)noexcept;
[[nodiscard]] b8 IsLiteralConstObjectType(MStringView name)noexcept;
[[nodiscard]] const BuiltinFormatArgWrapInfo* FindFormatArgWrapInfo(u8 wrapKindId)noexcept;
[[nodiscard]] MStringView FindFloatSimdRegisterTypeName(b8 isDouble)noexcept;
[[nodiscard]] MStringView FindIntSimdStorageVectorTypeName(b8 isDouble, b8 isSigned)noexcept;
[[nodiscard]] const BuiltinContainerArgConstraintInfo* FindContainerArgConstraint(BuiltinFunctionArgRule::Enum rule)noexcept;
[[nodiscard]] SpecialFunctionKind::Enum FindSpecialFunctionKind(MStringView name)noexcept;
[[nodiscard]] SimdLaneZeroRuleKind::Enum FindSimdLaneZeroRule(MStringView name)noexcept;
[[nodiscard]] SpecialMethodKind::Enum FindSpecialMethodSemantics(MStringView methodName)noexcept;

template<typename ResolveTypeFn, typename ParseCountFn>
[[nodiscard]] b8 ResolveInlineGenericLayout(
    const BuiltinGenericTypeInfo& genericInfo,
    const TypeRef& type,
    const u32 depth,
    usize& outSize,
    usize& outAlign,
    const ResolveTypeFn& resolveType,
    const ParseCountFn& parseCount
)noexcept{
    switch(genericInfo.inlineLayoutKind){
    case InlineLayoutKind::Atomic:{
        if(!type.templateArgs.empty() && type.templateArgs[0].type){
            resolveType(*type.templateArgs[0].type, depth + 1u, outSize, outAlign);
            return true;
        }
        outSize = 8u;
        outAlign = 8u;
        return true;
    }
    case InlineLayoutKind::Optional:{
        if(!type.templateArgs.empty() && type.templateArgs[0].type){
            usize innerSize = 8u;
            usize innerAlign = 8u;
            resolveType(*type.templateArgs[0].type, depth + 1u, innerSize, innerAlign);
            outAlign = innerAlign;
            outSize = ::AlignUp(innerSize + 1u, innerAlign);
            return true;
        }
        outSize = 16u;
        outAlign = 8u;
        return true;
    }
    case InlineLayoutKind::Expected:{
        if(!type.templateArgs.empty() && type.templateArgs[0].type){
            usize valSize = 8u;
            usize valAlign = 8u;
            usize errSize = 1u;
            usize errAlign = 1u;
            resolveType(*type.templateArgs[0].type, depth + 1u, valSize, valAlign);
            if(type.templateArgs.size() >= 2u && type.templateArgs[1].type)
                resolveType(*type.templateArgs[1].type, depth + 1u, errSize, errAlign);
            outAlign = valAlign > errAlign ? valAlign : errAlign;
            const usize payloadSize = valSize > errSize ? valSize : errSize;
            outSize = ::AlignUp(payloadSize + 1u, outAlign);
            return true;
        }
        outSize = 24u;
        outAlign = 8u;
        return true;
    }
    case InlineLayoutKind::Array:
    case InlineLayoutKind::FixedVector:{
        usize elemSize = 4u;
        usize elemAlign = 4u;
        usize count = 1u;
        if(!type.templateArgs.empty() && type.templateArgs[0].type)
            resolveType(*type.templateArgs[0].type, depth + 1u, elemSize, elemAlign);
        if(type.templateArgs.size() >= 2u)
            count = parseCount(type.templateArgs[1].constantOrTypeName());
        if(genericInfo.inlineLayoutKind == InlineLayoutKind::Array){
            outSize = elemSize * count;
            outAlign = elemAlign;
        }
        else{
            outAlign = elemAlign > 4u ? elemAlign : 4u;
            outSize = ::AlignUp(elemSize * count + 4u, outAlign);
        }
        return true;
    }
    case InlineLayoutKind::None:
        break;
    }
    return false;
}

template<typename ResolveFieldFn>
void ComputeSequentialExternClassLayout(
    const ClassDecl& classDecl,
    const b8 hasVtablePointer,
    const ResolveFieldFn& resolveField,
    usize& outSize,
    usize& outAlign
)noexcept{
    usize offset = hasVtablePointer ? 8u : 0u;
    usize maxAlign = offset > 0u ? 8u : 1u;
    b8 hasField = false;
    for(FieldDecl* field : classDecl.fields){
        if(!field || !field->type || field->isStatic)
            continue;
        hasField = true;
        usize fieldSize = 8u;
        usize fieldAlign = 8u;
        resolveField(*field->type, fieldSize, fieldAlign);
        field->resolvedSize = fieldSize;
        field->resolvedAlign = fieldAlign;
        if(fieldAlign > maxAlign)
            maxAlign = fieldAlign;
        offset = ::AlignUp(offset, fieldAlign);
        field->resolvedOffset = offset;
        offset += fieldSize;
    }
    if(hasField){
        outAlign = maxAlign;
        outSize = offset == 0u ? 8u : ::AlignUp(offset, maxAlign);
    }
    else{
        outSize = 8u;
        outAlign = 8u;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

