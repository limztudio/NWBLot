// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "type_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class TypeLayoutAnalyzer final{
public:
    explicit TypeLayoutAnalyzer(const SymbolTable& symbols)noexcept
        : m_symbols(symbols)
    {}

    [[nodiscard]] b8 containsArenaBackedContainer(const TypeRef& type, u32 depth = 0u)const noexcept;
    [[nodiscard]] b8 isNonRootMotherArenaType(const TypeRef& type, u32 depth = 0u)const noexcept;
    [[nodiscard]] b8 requiresMotherArena(const TypeRef& type, u32 depth = 0u)const noexcept;
    [[nodiscard]] b8 isFixedArrayNeedingArenaDefaultInit(const TypeRef& type)const noexcept;
    [[nodiscard]] b8 isClassCopyConstructible(const ClassDecl& decl, Span<const FieldDecl* const> fields, u32 depth = 0u)const noexcept;
    [[nodiscard]] b8 isClassMoveConstructible(const ClassDecl& decl, Span<const FieldDecl* const> fields, u32 depth = 0u)const noexcept;
    [[nodiscard]] b8 isClassCopyAssignable(const ClassDecl& decl, Span<const FieldDecl* const> fields, u32 depth = 0u)const noexcept;
    [[nodiscard]] b8 isClassMoveAssignable(const ClassDecl& decl, Span<const FieldDecl* const> fields, u32 depth = 0u)const noexcept;
    [[nodiscard]] b8 containsSimdRegisterType(const TypeRef& type, u32 depth = 0u)const noexcept;
    [[nodiscard]] b8 shouldPackSerializeClass(Span<const FieldDecl* const> fields)const noexcept;


private:
    [[nodiscard]] b8 interfaceInheritsNoCopy(const TypeRef& ifaceRef, u32 depth = 0u)const noexcept;
    [[nodiscard]] b8 isTypeConstructible(const TypeRef& type, b8 requireCopy, u32 depth = 0u)const noexcept;
    [[nodiscard]] b8 isTypeAssignable(const TypeRef& type, b8 requireCopy, u32 depth = 0u)const noexcept;
    [[nodiscard]] b8 isClassConstructible(const ClassDecl& decl, Span<const FieldDecl* const> fields, b8 requireCopy, u32 depth = 0u)const noexcept;
    [[nodiscard]] b8 isClassAssignableImpl(const ClassDecl& decl, Span<const FieldDecl* const> fields, b8 requireCopy, u32 depth = 0u)const noexcept;


private:
    const SymbolTable& m_symbols;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

