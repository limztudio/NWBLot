// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "const_eval.h"


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
    };
};

namespace BuiltinFunctionArgRule{
    enum Enum : u8{
#define NWB_ADHD_EXPORT_FUNCTION_ARG_RULE(ruleName, expectedParamType, isMut, requiredExactType, requiresStringLike, requiresIntegerScalar, errorMessage) \
        ruleName,
#include "../export_functions.h"
#undef NWB_ADHD_EXPORT_FUNCTION_ARG_RULE
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
    b8 arenaFirstInCtor;
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
};

struct BuiltinReceiverMethodInfo{
    MStringView receiverTypeName;
    MStringView methodName;
    MStringView returnTypeName;
    MStringView returnTypeArgName;
    MStringView returnErrorTypeName;
    b8 synthesizeTypeRef;
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
    Array<BuiltinFunctionArgRule::Enum, 5> argRules;
};

struct BuiltinGlobalConstantInfo{
    MStringView adhdName;
    MStringView cppExportName;
    MStringView adhdTypeName;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] constexpr b8 IsFieldReflected(const FieldDecl& field)noexcept{
    return !field.isTransient && (field.access == AccessSpecifier::Public || field.isReflect);
}

[[nodiscard]] constexpr b8 IsMethodReflected(const MethodDecl& method)noexcept{
    return !method.isConstructor && !method.isDestructor && (method.access == AccessSpecifier::Public || method.isReflect);
}

[[nodiscard]] constexpr b8 IsEnumEntryReflected(const EnumEntry& entry)noexcept{
    return entry.name != "kCount";
}

[[nodiscard]] const BuiltinObjectTypeInfo* FindBuiltinObjectType(MStringView name)noexcept;
[[nodiscard]] const BuiltinEnumInfo* FindBuiltinEnum(MStringView name)noexcept;
[[nodiscard]] b8 HasBuiltinEnumEntry(MStringView enumName, MStringView entryName)noexcept;
[[nodiscard]] b8 IsArenaFirstCtorType(MStringView name)noexcept;
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
[[nodiscard]] const BuiltinFieldInfo* FindBuiltinField(MStringView receiverTypeName, MStringView fieldName)noexcept;
[[nodiscard]] const BuiltinFunctionArgRuleInfo* FindBuiltinFunctionArgRule(BuiltinFunctionArgRule::Enum rule)noexcept;
[[nodiscard]] const BuiltinFunctionInfo* FindBuiltinFunction(MStringView name)noexcept;
[[nodiscard]] const BuiltinGlobalConstantInfo* FindBuiltinGlobalConstant(MStringView name)noexcept;
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
[[nodiscard]] b8 IsAtomicGenericType(MStringView name)noexcept;
[[nodiscard]] b8 IsAssetHandleType(MStringView name)noexcept;
[[nodiscard]] b8 CheckSwizzlePattern(MStringView text, u32 maxLanes, b8& outIsPattern, b8& outValidLanes)noexcept;

[[nodiscard]] b8 IsVoidTypeRef(const TypeRef* ref)noexcept;

[[nodiscard]] i32 ScoreCallableParams(const SymbolTable& symbols, Span<const ParamDecl> params, Span<const MStringView> argTypeNames, Span<const u8> argIsMut)noexcept;

[[nodiscard]] b8 IsReflectedLinearContainerType(MStringView baseName)noexcept;
[[nodiscard]] b8 IsReflectedMapContainerType(MStringView baseName)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

