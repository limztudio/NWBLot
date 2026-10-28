// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../sema/type_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] constexpr bool IsFieldReflected(const FieldDecl& field)noexcept{
    return !field.isTransient && (field.access == AccessSpecifier::Public || field.isReflect);
}

[[nodiscard]] constexpr bool IsReflectedLinearContainerType(const MStringView tname)noexcept{
    return tname == "Array" || tname == "FixedVector" || tname == "Vector"
        || tname == "Deque" || tname == "ParallelVector" || tname == "List"
        || tname == "ForwardList" || tname == "Set" || tname == "HashSet"
        || tname == "ParallelHashSet";
}

[[nodiscard]] constexpr bool IsReflectedMapContainerType(const MStringView tname)noexcept{
    return tname == "Map" || tname == "HashMap" || tname == "ParallelHashMap";
}

[[nodiscard]] bool IsAssetDependencyType(const TypeRef* type, const SymbolTable& symbols, u32 depth = 0u)noexcept;
[[nodiscard]] bool IsAssetDependencyField(const FieldDecl& field, const SymbolTable& symbols, u32 depth = 0u)noexcept;


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
