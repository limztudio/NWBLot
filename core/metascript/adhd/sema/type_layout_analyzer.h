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

    [[nodiscard]] b8 isNonRootMotherArenaType(const TypeRef& type, u32 depth = 0u)const noexcept;
    [[nodiscard]] b8 requiresMotherArena(const TypeRef& type, u32 depth = 0u)const noexcept;
    [[nodiscard]] b8 isArenaBackedValueWrapper(const TypeRef& type)const noexcept;
    [[nodiscard]] b8 isFixedArrayNeedingArenaDefaultInit(const TypeRef& type)const noexcept;
    [[nodiscard]] b8 isClassCopyConstructible(const ClassDecl& decl, Span<const FieldDecl* const> fields)const noexcept;
    [[nodiscard]] b8 isClassMoveConstructible(const ClassDecl& decl, Span<const FieldDecl* const> fields)const noexcept;
    [[nodiscard]] b8 isClassCopyAssignable(const ClassDecl& decl, Span<const FieldDecl* const> fields)const noexcept;
    [[nodiscard]] b8 isClassMoveAssignable(const ClassDecl& decl, Span<const FieldDecl* const> fields)const noexcept;
    [[nodiscard]] b8 isClassNothrowDefaultConstructible(const ClassDecl& decl, Span<const FieldDecl* const> fields, u32 depth = 0u)const noexcept;
    [[nodiscard]] b8 containsSimdRegisterType(const TypeRef& type, u32 depth = 0u)const noexcept;
    [[nodiscard]] b8 shouldPackSerializeClass(Span<const FieldDecl* const> fields, u32 depth = 0u)const noexcept;
    [[nodiscard]] b8 isTypeConstructible(const TypeRef& type, b8 requireCopy)const noexcept;
    [[nodiscard]] b8 isTypeAssignable(const TypeRef& type, b8 requireCopy)const noexcept;
    [[nodiscard]] b8 interfaceInheritsNoCopy(const TypeRef& ifaceRef, u32 depth = 0u)const noexcept;
    [[nodiscard]] b8 isOrderIndependentFieldType(const TypeRef& type, u32 depth = 0u)const noexcept;
    [[nodiscard]] b8 isOrderIndependentInitExpr(const Expr* expr, u32 depth = 0u)const noexcept;
    [[nodiscard]] b8 requiresDeclarationFieldOrder(const ClassDecl& decl, Span<const FieldDecl* const> fields)const noexcept;


private:
    [[nodiscard]] b8 areFieldDeclsAndInitsOrderIndependent(Span<const FieldDecl* const> fields, const ClassDecl* substOwner, const TypeRef* instance, Span<const TypeArg> templateArgs, u32 depth)const noexcept;
    [[nodiscard]] b8 areClassFieldsAndInitsOrderIndependent(const ClassDecl& cls, const TypeRef* instance, Span<const TypeArg> templateArgs, u32 depth)const noexcept;
    [[nodiscard]] b8 isOrderIndependentCallInitExpr(const Expr& expr, u32 depth)const noexcept;
    [[nodiscard]] b8 containsOverAlignedPackType(const TypeRef& type, u32 depth = 0u)const noexcept;
    struct ValueTypeVisit{
        const ClassDecl& decl;
        const TypeRef* instance;
        const ValueTypeVisit* parent;
    };

    [[nodiscard]] b8 supportsTypeOperation(const TypeRef& type, b8 requireCopy, b8 assign, const ValueTypeVisit* active)const noexcept;
    [[nodiscard]] b8 supportsClassOperation(const ClassDecl& decl, Span<const FieldDecl* const> fields, const TypeRef* instance, b8 requireCopy, b8 assign, const ValueTypeVisit* active)const noexcept;


private:
    const SymbolTable& m_symbols;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

