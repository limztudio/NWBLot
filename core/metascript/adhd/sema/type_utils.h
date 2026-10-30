// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "const_eval.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GenericParamKind{
    enum Enum : u8{
        SingleType,
        TwoType,
        TypeAndCount,
        NotNullType,
    };
};

namespace BuiltinMemberReturnRule{
    enum Enum : u8{
        None = 0u,
        Void,
        Usize,
        Bool,
        StringView,
        OptionalOrExpectedValue,
        ExpectedError,
        FirstTypeArg,
        AtomicValue,
    };
};

namespace BuiltinFunctionReturnRule{
    enum Enum : u8{
        PromoteSimd,
        LaneScalar,
        MatrixMultiply,
        Bool,
        Usize,
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct BuiltinObjectTypeInfo{
    MStringView adhdName;
    MStringView cppExportName;
    u32 byteSize;
    u32 byteAlign;
    b8 isArenaBacked;
    b8 isSelfDefaulted;
    b8 isSmallValue;
    b8 isStringSwitch;
    b8 isNonThreadSafeContainer;
    b8 isNwbMapKey;
};

struct BuiltinGenericTypeInfo{
    MStringView adhdName;
    MStringView cppExportName;
    GenericParamKind::Enum paramKind;
    u32 byteSize;
    u32 byteAlign;
    b8 isArenaBacked;
    b8 isSelfDefaulted;
    b8 isSmallValue;
    b8 isNonThreadSafeContainer;
    b8 isNonRandomAccessContainer;
    b8 isIndexableSequence;
    b8 isReflectedLinear;
    b8 isReflectedMap;
};

struct BuiltinUnalignedVectorInfo{
    MStringView adhdName;
    MStringView cppExportName;
    MStringView laneScalarType;
    u32 lanes;
    u32 byteSize;
    u32 byteAlign;
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
    BuiltinMemberReturnRule::Enum returnRule;
    b8 isMutating;
    b8 adaptsFirstArgToElement;
};

struct BuiltinFunctionInfo{
    MStringView adhdName;
    MStringView cppExportName;
    usize arity;
    BuiltinFunctionReturnRule::Enum returnRule;
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

[[nodiscard]] const BuiltinObjectTypeInfo* FindBuiltinObjectType(MStringView name)noexcept;
[[nodiscard]] const BuiltinGenericTypeInfo* FindBuiltinGenericType(MStringView name)noexcept;
[[nodiscard]] const BuiltinUnalignedVectorInfo* FindBuiltinUnalignedVectorType(MStringView name)noexcept;
[[nodiscard]] const BuiltinSimdVectorInfo* FindBuiltinSimdVectorType(MStringView name)noexcept;
[[nodiscard]] const BuiltinSimdMatrixInfo* FindBuiltinSimdMatrixType(MStringView name)noexcept;
[[nodiscard]] b8 FindBuiltinFixedLayout(MStringView name, usize& outSize, usize& outAlign)noexcept;
[[nodiscard]] const BuiltinMemberMethodInfo* FindBuiltinMemberMethod(MStringView name)noexcept;
[[nodiscard]] const BuiltinFunctionInfo* FindBuiltinFunction(MStringView name)noexcept;
[[nodiscard]] const BuiltinGlobalConstantInfo* FindBuiltinGlobalConstant(MStringView name)noexcept;
[[nodiscard]] MStringView FindBuiltinGlobalConstantType(MStringView name)noexcept;
[[nodiscard]] MStringView FindBuiltinGlobalConstantCppName(MStringView name)noexcept;
[[nodiscard]] b8 IsNwbMapKeyType(MStringView name)noexcept;

[[nodiscard]] b8 IsNonThreadSafeContainer(MStringView typeName)noexcept;
[[nodiscard]] b8 IsSingleTypeParamBuiltinGeneric(MStringView name)noexcept;
[[nodiscard]] b8 IsTwoTypeParamBuiltinGeneric(MStringView name)noexcept;
[[nodiscard]] b8 IsTypeAndCountBuiltinGeneric(MStringView name)noexcept;
[[nodiscard]] b8 CheckSwizzlePattern(MStringView text, u32 maxLanes, b8& outIsPattern, b8& outValidLanes)noexcept;

[[nodiscard]] b8 IsVoidTypeRef(const TypeRef* ref)noexcept;
[[nodiscard]] b8 AreTypeRefsEquivalent(const TypeRef* a, const TypeRef* b, const SymbolTable& symbols)noexcept;
[[nodiscard]] b8 AreTypeRefsEquivalentWithSubst(const TypeRef* actual, const TypeRef* pattern, Span<const TemplateParamDecl> ifaceParams, Span<const TypeArg> ifaceArgs, const SymbolTable& symbols)noexcept;
[[nodiscard]] b8 AreTemplateArgListsEqual(Span<const TypeArg> a, Span<const TypeArg> b, const SymbolTable& symbols)noexcept;
[[nodiscard]] b8 DoParamListsMatch(Span<const ParamDecl> a, Span<const ParamDecl> b, const SymbolTable& symbols)noexcept;

[[nodiscard]] TypeRef* ExtractUnderlyingFuncType(TypeRef* type, const SymbolTable& symbols)noexcept;
[[nodiscard]] b8 IsKnownConcreteTypeName(MStringView name, const SymbolTable& symbols)noexcept;
[[nodiscard]] b8 IsConcreteTypeRef(const TypeRef& type, const SymbolTable& symbols)noexcept;
[[nodiscard]] b8 IsConcreteTemplateArgList(Span<const TemplateParamDecl> params, Span<const TypeArg> args, const SymbolTable& symbols)noexcept;
[[nodiscard]] b8 TypeContainsArenaBackedContainer(const TypeRef& type, const SymbolTable& symbols, u32 depth = 0u)noexcept;
[[nodiscard]] b8 ShouldPackSerializeClass(Span<const FieldDecl* const> fields, const SymbolTable& symbols)noexcept;
[[nodiscard]] i32 ScoreCallableParams(const SymbolTable& symbols, Span<const ParamDecl> params, Span<const MStringView> argTypeNames, Span<const u8> argIsMut)noexcept;

[[nodiscard]] b8 IsReflectedLinearContainerType(MStringView baseName)noexcept;
[[nodiscard]] b8 IsReflectedMapContainerType(MStringView baseName)noexcept;
[[nodiscard]] const TypeRef* UnwrapOptionalFieldType(const TypeRef* fieldType, const SymbolTable& symbols, b8& outIsOptional, MStringView& outBaseTypeName)noexcept;
[[nodiscard]] b8 IsAssetDependencyType(const TypeRef* type, const SymbolTable& symbols, u32 depth = 0u)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

