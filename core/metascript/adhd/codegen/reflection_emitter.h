// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "cpp_type_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ReflectionEmitter final{
public:
    explicit ReflectionEmitter(const SymbolTable& symbols)noexcept
        : m_symbols(symbols)
    {}


public:
    void emitInterfaceReflectionDeclarations(const InterfaceDecl& decl, MetaArena& arena, MString& out)const;
    void emitClassReflectionDeclarations(MString& out, const ClassDecl& decl)const;
    void emitInlineClassReflection(MString& out, const ClassDecl& decl, Span<const FieldDecl* const> fields, b8 classNeedsArena)const;
    void emitClassReflectionDefinitions(MString& out, const ClassDecl& decl)const;


private:
    void emitClassReflectionMembers(MString& out, const ClassDecl& decl, Span<const FieldDecl* const> fields, b8 inlineBodies, b8 classNeedsArena)const;
    void emitAssetDependencyMethods(MString& out, Span<const FieldDecl* const> fields)const;


private:
    const SymbolTable& m_symbols;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

