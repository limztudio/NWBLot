// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "cpp_type_emitter.h"
#include "source_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class HeaderEmitter{
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
    void emitEnumDecl(MString& out, const EnumDecl& decl)const;
    void emitTypeAliasDecl(MString& out, const TypeAliasDecl& decl)const;
    void emitInterfaceDecl(MString& out, const InterfaceDecl& decl)const;
    void emitGlobalConstDecl(MString& out, const Stmt& decl)const;
    void emitFreeFunctionDecl(MString& out, const MethodDecl& fn)const;
    void emitClassDecl(MString& out, const ClassDecl& decl)const;
    void emitClassTemplateSpecialization(MString& out, const ClassTemplateInstantiation& inst)const;
    void emitClassMethodsCpp(MString& out, const ClassDecl& decl, const MVector<FieldDecl*>& orderedFields, bool forceInlineBody)const;
    void emitClassFieldsCpp(MString& out, const MVector<FieldDecl*>& orderedFields)const;


private:
    MetaArena& m_arena;
    const SymbolTable& m_symbols;
    CppTypeEmitter m_typeEmitter;
    SourceEmitter m_sourceEmitter;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
