// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "cpp_type_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ReflectionEmitter final{
public:
    ReflectionEmitter(const SymbolTable& symbols, const CppTypeEmitter& typeEmitter)noexcept
        : m_symbols(symbols)
        , m_typeEmitter(typeEmitter)
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
    const CppTypeEmitter& m_typeEmitter;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

