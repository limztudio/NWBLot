// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../sema/type_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] constexpr b8 IsFieldReflected(const FieldDecl& field)noexcept{
    return !field.isTransient && (field.access == AccessSpecifier::Public || field.isReflect);
}

[[nodiscard]] constexpr b8 IsReflectedLinearContainerType(const MStringView tname)noexcept{
#define NWB_ADHD_EXPORT_GENERIC_TYPE(adhdName, cppExportName, paramKind, byteSize, byteAlign, isArenaBacked, isSelfDefaulted, isSmallValue, isNonThreadSafeContainer, isNonRandomAccessContainer, isIndexableSequence, isReflectedLinear, isReflectedMap) \
    if constexpr(isReflectedLinear){ \
        if(tname == adhdName) return true; \
    }
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_GENERIC_TYPE
    return false;
}

[[nodiscard]] constexpr b8 IsReflectedMapContainerType(const MStringView tname)noexcept{
#define NWB_ADHD_EXPORT_GENERIC_TYPE(adhdName, cppExportName, paramKind, byteSize, byteAlign, isArenaBacked, isSelfDefaulted, isSmallValue, isNonThreadSafeContainer, isNonRandomAccessContainer, isIndexableSequence, isReflectedLinear, isReflectedMap) \
    if constexpr(isReflectedMap){ \
        if(tname == adhdName) return true; \
    }
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_GENERIC_TYPE
    return false;
}

[[nodiscard]] b8 IsAssetDependencyType(const TypeRef* type, const SymbolTable& symbols, u32 depth = 0u)noexcept;
[[nodiscard]] b8 IsAssetDependencyField(const FieldDecl& field, const SymbolTable& symbols, u32 depth = 0u)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ReflectionEmitter{
public:
    explicit ReflectionEmitter(const SymbolTable& symbols)noexcept
        : m_symbols(symbols)
    {}


public:
    void emitClassReflectionDeclarations(MString& out, const ClassDecl& decl)const;
    void emitClassReflectionDefinitions(MString& out, const ClassDecl& decl)const;
    void emitInlineClassReflection(MString& out, const ClassDecl& decl, const MVector<FieldDecl*>& fields)const;


private:
    void emitDeserializeNwbMethod(MString& out, const MVector<FieldDecl*>& fields)const;
    void emitAssetDependencyMethods(MString& out, const MVector<FieldDecl*>& fields)const;


private:
    const SymbolTable& m_symbols;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
