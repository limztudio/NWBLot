// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "source_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class HeaderEmitter final{
public:
    HeaderEmitter(MetaArena& arena, const SymbolTable& symbols)noexcept
        : m_arena(arena)
        , m_symbols(symbols)
        , m_typeEmitter(symbols)
        , m_sourceEmitter(arena, symbols)
    {}


public:
    [[nodiscard]] MString emitHeader(const TranslationUnit& unit, MStringView namespacePath)const;


private:
    [[nodiscard]] b8 tryResolveSubsystemFieldAssetType(const FieldDecl& field, MStringView& outAssetTypeName, b8& outIsVector)const noexcept;
    void emitEnumDecl(MString& out, const EnumDecl& decl)const;
    void emitTypeAliasDecl(MString& out, const TypeAliasDecl& decl)const;
    void emitInterfaceDecl(MString& out, const InterfaceDecl& decl)const;
    void emitGlobalConstDecl(MString& out, const Stmt& decl)const;
    void emitClassDecl(MString& out, const ClassDecl& decl)const;
    void emitClassTemplateSpecialization(MString& out, const ClassTemplateInstantiation& inst)const;
    void emitClassMethodsCpp(MString& out, const ClassDecl& decl, Span<const FieldDecl* const> orderedFields, b8 forceInlineBody)const;
    void emitClassFieldsCpp(MString& out, Span<const FieldDecl* const> orderedFields)const;


private:
    MetaArena& m_arena;
    const SymbolTable& m_symbols;
    CppTypeEmitter m_typeEmitter;
    SourceEmitter m_sourceEmitter;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

