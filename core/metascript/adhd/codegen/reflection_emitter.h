// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "cpp_type_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ReflectionEmitter{
public:
    explicit ReflectionEmitter(const SymbolTable& symbols)noexcept
        : m_symbols(symbols)
    {}


public:
    void emitInterfaceReflectionDeclarations(MString& out, const InterfaceDecl& decl, MetaArena& arena)const;
    void emitClassReflectionDeclarations(MString& out, const ClassDecl& decl)const;
    void emitInlineClassReflection(MString& out, const ClassDecl& decl, Span<const FieldDecl* const> fields)const;
    void emitClassReflectionDefinitions(MString& out, const ClassDecl& decl)const;


private:
    void emitAssetDependencyMethods(MString& out, Span<const FieldDecl* const> fields)const;
    void emitDeserializeNwbMethod(MString& out, Span<const FieldDecl* const> fields)const;
    void emitExtractNwbAssetDependenciesMethod(MString& out, Span<const FieldDecl* const> fields)const;


private:
    const SymbolTable& m_symbols;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

