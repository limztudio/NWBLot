// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/metascript/preprocessor.h>

#include "sema/symbol_table.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct CompileResult{
    TranslationUnit unit;
    SymbolTable symbols;
    MString preprocessedSource;
    MString generatedHeader;
    MString generatedSource;
    MVector<ParseError> errors;


public:
    explicit CompileResult(MetaArena& arena)noexcept
        : unit(arena)
        , symbols(arena)
        , preprocessedSource(arena)
        , generatedHeader(arena)
        , generatedSource(arena)
        , errors(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Compiler{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 Compile(MStringView source, MStringView virtualFilePath, IIncludeResolver* includeResolver, MStringView customNamespace, Span<const MString> macroDefinitions, MetaArena& arena, CompileResult& outResult);
[[nodiscard]] inline b8 Compile(const MStringView source, const MStringView virtualFilePath, IIncludeResolver* includeResolver, MetaArena& arena, CompileResult& outResult){ return Compile(source, virtualFilePath, includeResolver, {}, {}, arena, outResult); }
[[nodiscard]] inline b8 Compile(const MStringView source, const MStringView virtualFilePath, MetaArena& arena, CompileResult& outResult){ return Compile(source, virtualFilePath, nullptr, {}, {}, arena, outResult); }

[[nodiscard]] MString DeriveOutputFilename(MStringView virtualFilePath, MStringView suffix, MetaArena& arena);
[[nodiscard]] MString DeriveNamespaceFromPath(MStringView virtualFilePath, MetaArena& arena);
[[nodiscard]] inline MString DeriveHeaderFilename(const MStringView virtualFilePath, MetaArena& arena){ return DeriveOutputFilename(virtualFilePath, ".adhd.h", arena); }
[[nodiscard]] inline MString DeriveSourceFilename(const MStringView virtualFilePath, MetaArena& arena){ return DeriveOutputFilename(virtualFilePath, ".adhd.cpp", arena); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

