// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../sema/symbol_table.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class HeaderEmitter{
public:
    HeaderEmitter(MetaArena& arena, const SymbolTable& symbols)noexcept
        : m_arena(arena)
        , m_symbols(symbols)
    {}


public:
    [[nodiscard]] MString emitHeader(const TranslationUnit& unit, MStringView namespacePath)const;

    void emitTypeCpp(MString& out, const TypeRef& type)const;
    void emitParamTypeCpp(MString& out, ParamPassingMode::Enum mode, const TypeRef& type)const;
    void emitTemplateHeaderCpp(MString& out, const MVector<TemplateParamDecl>& params)const;
    void emitMethodSignature(MString& out, const ClassDecl* ownerClass, const MethodDecl& method, bool isDeclarationInClass)const;
    void emitConstructorNotNullInitializers(MString& out, const MVector<FieldDecl*>& orderedFields, const MethodDecl& ctor)const;


private:
    void emitEnumDecl(MString& out, const EnumDecl& decl)const;
    void emitTypeAliasDecl(MString& out, const TypeAliasDecl& decl)const;
    void emitInterfaceDecl(MString& out, const InterfaceDecl& decl)const;
    void emitClassDecl(MString& out, const ClassDecl& decl)const;
    void emitClassTemplateSpecialization(MString& out, const ClassTemplateInstantiation& inst)const;
    [[nodiscard]] static bool isSmallValueType(MStringView typeName, const SymbolTable& symbols)noexcept;


private:
    MetaArena& m_arena;
    const SymbolTable& m_symbols;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

