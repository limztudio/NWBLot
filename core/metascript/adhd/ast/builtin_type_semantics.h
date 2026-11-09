// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "ast_common.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct BuiltinScalarTypeInfo{
    MStringView adhdName;
    u32 byteSize;
    u32 byteAlign;
    b8 isInteger;
    b8 isFixedWidthInt;
    b8 isSigned;
    b8 isFloat;
};


class BuiltinTypeSemantics final{
public:
    inline static constexpr auto s_BuiltinScalarTypes = Array{
    #define NWB_ADHD_EXPORT_SCALAR_TYPE(adhdName, cppType, byteSize, byteAlign, isInteger, isFixedWidthInt, isSigned, isFloat) \
        BuiltinScalarTypeInfo{ adhdName, byteSize, byteAlign, isInteger, isFixedWidthInt, isSigned, isFloat },
    #include "../export_types.h"
    #undef NWB_ADHD_EXPORT_SCALAR_TYPE
    };

    [[nodiscard]] static constexpr const BuiltinScalarTypeInfo* FindBuiltinScalarType(const MStringView name)noexcept{
        for(const BuiltinScalarTypeInfo& info : s_BuiltinScalarTypes){
            if(info.adhdName == name)
                return &info;
        }
        return nullptr;
    }

    [[nodiscard]] static constexpr b8 IsScalarType(const MStringView name)noexcept{
        return FindBuiltinScalarType(name) != nullptr;
    }

    [[nodiscard]] static constexpr b8 IsIntegerScalarType(const MStringView name)noexcept{
        const BuiltinScalarTypeInfo* info = FindBuiltinScalarType(name);
        return info && info->isInteger;
    }

    [[nodiscard]] static constexpr b8 IsFloatScalarType(const MStringView name)noexcept{
        const BuiltinScalarTypeInfo* info = FindBuiltinScalarType(name);
        return info && info->isFloat;
    }

    [[nodiscard]] static constexpr b8 IsNumericScalarType(const MStringView name)noexcept{
        const BuiltinScalarTypeInfo* info = FindBuiltinScalarType(name);
        return info && (info->isInteger || info->isFloat);
    }

    [[nodiscard]] static constexpr b8 IsIntegralOrBoolScalarType(const MStringView name)noexcept{
        const BuiltinScalarTypeInfo* info = FindBuiltinScalarType(name);
        return info && !info->isFloat;
    }

    [[nodiscard]] static constexpr b8 IsBoolScalarType(const MStringView name)noexcept{
        const BuiltinScalarTypeInfo* info = FindBuiltinScalarType(name);
        return info && !info->isInteger && !info->isFloat;
    }

    [[nodiscard]] static constexpr b8 TryMapScalarLimitMember(const MStringView member, MStringView* outCppLimitSuffix = nullptr, b8* outIsMin = nullptr)noexcept{
    #define NWB_ADHD_EXPORT_SCALAR_LIMIT(memberName, cppLimitSuffix, isMin) \
        if(member == memberName){ \
            if(outCppLimitSuffix) *outCppLimitSuffix = cppLimitSuffix; \
            if(outIsMin) *outIsMin = isMin; \
            return true; \
        }
    #include "../export_types.h"
    #undef NWB_ADHD_EXPORT_SCALAR_LIMIT
        return false;
    }

    [[nodiscard]] static constexpr b8 ResolveFixedWidthIntScalar(const MStringView name, usize& outSize, usize& outAlign, b8* outIsSigned = nullptr)noexcept{
        const BuiltinScalarTypeInfo* info = FindBuiltinScalarType(name);
        if(!info || !info->isFixedWidthInt)
            return false;
        outSize = info->byteSize;
        outAlign = info->byteAlign;
        if(outIsSigned)
            *outIsSigned = info->isSigned;
        return true;
    }

    [[nodiscard]] static constexpr b8 IsBuiltinTypeName(const MStringView name)noexcept{
        if(name == "void" || name == "Func" || IsScalarType(name))
            return true;
    #define NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE(adhdName, cppExportName, lanes, altCtorArity, byteSize, byteAlign, xyzScalarType, wScalarType, isFloatReg, isDoubleReg, isIntReg, isSignedInt, isMixed, regCppType, loadCppFunc, storeCppFunc) \
        if(name == adhdName) return true;
    #define NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE(adhdName, cppExportName, elemCount, byteSize, byteAlign, elemScalarType, rowVectorType, isDoubleReg, regCppType, loadCppFunc, storeCppFunc) \
        if(name == adhdName) return true;
    #define NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE(adhdName, cppExportName, lanes, altCtorArity, byteSize, byteAlign, laneScalarType, isHalfFloat) \
        if(name == adhdName) return true;
    #define NWB_ADHD_EXPORT_OBJECT_TYPE(adhdName, cppExportName, byteSize, byteAlign, isArenaBacked, isSelfDefaulted, isSmallValue, isBorrowView, isBinarySerializable, isSingleValueCtor, emitsBraceFieldInit, isStringLike, isStringSwitch, isNonThreadSafeContainer, isNwbMapKey, requiresTCharAdaptation) \
        if(name == adhdName) return true;
    #define NWB_ADHD_EXPORT_GENERIC_TYPE(adhdName, cppExportName, paramKind, inlineLayoutKind, byteSize, byteAlign, isArenaBacked, isSelfDefaulted, isSmallValue, isBorrowView, isBinarySerializable, isSingleValueCtor, emitsBraceFieldInit, isNonThreadSafeContainer, isNonRandomAccessContainer, isIndexableSequence, reflectedLinearKind, isReflectedMap, isAssetHandle) \
        if(name == adhdName) return true;
    #include "../export_types.h"
    #undef NWB_ADHD_EXPORT_GENERIC_TYPE
    #undef NWB_ADHD_EXPORT_OBJECT_TYPE
    #undef NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE
    #undef NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE
    #undef NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE
        return false;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

