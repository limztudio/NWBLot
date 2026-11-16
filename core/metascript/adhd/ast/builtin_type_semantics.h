// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "ast_common.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SpecialBuiltinTypeKind{
    enum Enum : u8{
        Void,
        Func,
        Bool,
        ByteScalar,
        Int32,
        UInt32,
        Int64,
        UInt64,
        Usize,
        Float32,
        Float64,
        StringView,
        Name,
        Path,
        VectorSingle,
        VectorDouble,
        ArenaBase,
    };
};

struct SpecialBuiltinTypeInfo{
    SpecialBuiltinTypeKind::Enum kind;
    MStringView adhdName;
    MStringView cppName;
    b8 isReservedPrimitive;
};

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
    inline static constexpr auto s_SpecialBuiltinTypes = Array{
    #define NWB_ADHD_EXPORT_SPECIAL_TYPE(kind, adhdName, cppName, isReservedPrimitive) \
        SpecialBuiltinTypeInfo{ SpecialBuiltinTypeKind::kind, adhdName, cppName, isReservedPrimitive },
    #include "../export_types.h"
    #undef NWB_ADHD_EXPORT_SPECIAL_TYPE
    };

    inline static constexpr auto s_BuiltinScalarTypes = Array{
    #define NWB_ADHD_EXPORT_SCALAR_TYPE(adhdName, cppType, byteSize, byteAlign, isInteger, isFixedWidthInt, isSigned, isFloat) \
        BuiltinScalarTypeInfo{ adhdName, byteSize, byteAlign, isInteger, isFixedWidthInt, isSigned, isFloat },
    #include "../export_types.h"
    #undef NWB_ADHD_EXPORT_SCALAR_TYPE
    };

    [[nodiscard]] static constexpr const SpecialBuiltinTypeInfo* FindSpecialBuiltinType(const SpecialBuiltinTypeKind::Enum kind)noexcept{
        for(const SpecialBuiltinTypeInfo& info : s_SpecialBuiltinTypes){
            if(info.kind == kind)
                return &info;
        }
        return nullptr;
    }

    [[nodiscard]] static constexpr MStringView SpecialTypeName(const SpecialBuiltinTypeKind::Enum kind)noexcept{
        const SpecialBuiltinTypeInfo* info = FindSpecialBuiltinType(kind);
        return info ? info->adhdName : MStringView{};
    }

    [[nodiscard]] static constexpr MStringView SpecialTypeCppName(const SpecialBuiltinTypeKind::Enum kind)noexcept{
        const SpecialBuiltinTypeInfo* info = FindSpecialBuiltinType(kind);
        return info ? info->cppName : MStringView{};
    }

    [[nodiscard]] static constexpr MStringView VoidTypeName()noexcept{ return SpecialTypeName(SpecialBuiltinTypeKind::Void); }
    [[nodiscard]] static constexpr MStringView FuncTypeName()noexcept{ return SpecialTypeName(SpecialBuiltinTypeKind::Func); }
    [[nodiscard]] static constexpr MStringView BoolScalarTypeName()noexcept{ return SpecialTypeName(SpecialBuiltinTypeKind::Bool); }
    [[nodiscard]] static constexpr MStringView ByteScalarTypeName()noexcept{ return SpecialTypeName(SpecialBuiltinTypeKind::ByteScalar); }
    [[nodiscard]] static constexpr MStringView Int32ScalarTypeName()noexcept{ return SpecialTypeName(SpecialBuiltinTypeKind::Int32); }
    [[nodiscard]] static constexpr MStringView UInt32ScalarTypeName()noexcept{ return SpecialTypeName(SpecialBuiltinTypeKind::UInt32); }
    [[nodiscard]] static constexpr MStringView Int64ScalarTypeName()noexcept{ return SpecialTypeName(SpecialBuiltinTypeKind::Int64); }
    [[nodiscard]] static constexpr MStringView UInt64ScalarTypeName()noexcept{ return SpecialTypeName(SpecialBuiltinTypeKind::UInt64); }
    [[nodiscard]] static constexpr MStringView UsizeScalarTypeName()noexcept{ return SpecialTypeName(SpecialBuiltinTypeKind::Usize); }
    [[nodiscard]] static constexpr MStringView Float32ScalarTypeName()noexcept{ return SpecialTypeName(SpecialBuiltinTypeKind::Float32); }
    [[nodiscard]] static constexpr MStringView Float64ScalarTypeName()noexcept{ return SpecialTypeName(SpecialBuiltinTypeKind::Float64); }
    [[nodiscard]] static constexpr MStringView StringViewTypeName()noexcept{ return SpecialTypeName(SpecialBuiltinTypeKind::StringView); }
    [[nodiscard]] static constexpr MStringView NameTypeName()noexcept{ return SpecialTypeName(SpecialBuiltinTypeKind::Name); }
    [[nodiscard]] static constexpr MStringView PathTypeName()noexcept{ return SpecialTypeName(SpecialBuiltinTypeKind::Path); }
    [[nodiscard]] static constexpr MStringView VectorSingleTypeName()noexcept{ return SpecialTypeName(SpecialBuiltinTypeKind::VectorSingle); }
    [[nodiscard]] static constexpr MStringView VectorDoubleTypeName()noexcept{ return SpecialTypeName(SpecialBuiltinTypeKind::VectorDouble); }
    [[nodiscard]] static constexpr MStringView ArenaBaseTypeName()noexcept{ return SpecialTypeName(SpecialBuiltinTypeKind::ArenaBase); }
    [[nodiscard]] static constexpr MStringView ArenaBaseCppName()noexcept{ return SpecialTypeCppName(SpecialBuiltinTypeKind::ArenaBase); }

    [[nodiscard]] static constexpr b8 IsVoidTypeName(const MStringView name)noexcept{
        return name == VoidTypeName();
    }

    [[nodiscard]] static constexpr b8 IsFuncTypeName(const MStringView name)noexcept{
        return name == FuncTypeName();
    }

    [[nodiscard]] static constexpr MStringView PromoteFloatScalarTypes(const MStringView lhs, const MStringView rhs)noexcept{
        const MStringView f64Name = Float64ScalarTypeName();
        if(lhs == f64Name || rhs == f64Name)
            return f64Name;
        const MStringView f32Name = Float32ScalarTypeName();
        if(lhs == f32Name || rhs == f32Name)
            return f32Name;
        return {};
    }

    [[nodiscard]] static constexpr MStringView FindIntegerScalarTypeName(const u32 byteSize, const b8 isSigned)noexcept{
        for(const BuiltinScalarTypeInfo& info : s_BuiltinScalarTypes){
            if(info.isInteger && info.isFixedWidthInt && info.byteSize == byteSize && info.isSigned == isSigned)
                return info.adhdName;
        }
        return {};
    }

    [[nodiscard]] static constexpr MStringView FindPointerSizedIntScalarTypeName(const b8 isSigned)noexcept{
        for(const BuiltinScalarTypeInfo& info : s_BuiltinScalarTypes){
            if(info.isInteger && !info.isFixedWidthInt && info.isSigned == isSigned)
                return info.adhdName;
        }
        return {};
    }

    [[nodiscard]] static constexpr MStringView FindFloatScalarTypeName(const u32 byteSize)noexcept{
        for(const BuiltinScalarTypeInfo& info : s_BuiltinScalarTypes){
            if(info.isFloat && info.byteSize == byteSize)
                return info.adhdName;
        }
        return {};
    }

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

    [[nodiscard]] static b8 IsReservedPrimitiveOrContainerTypeName(MStringView name)noexcept;
    [[nodiscard]] static b8 IsBuiltinTypeName(MStringView name)noexcept;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

