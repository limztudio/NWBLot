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
        , symbols(arena, unit)
        , preprocessedSource(arena)
        , generatedHeader(arena)
        , generatedSource(arena)
        , errors(arena)
    {}
    CompileResult(const CompileResult&) = delete;
    CompileResult(CompileResult&&) = delete;
    CompileResult& operator=(const CompileResult&) = delete;
    CompileResult& operator=(CompileResult&&) = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Compiler{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct CompileOptions{
    MStringView source;
    MStringView virtualFilePath;
    IIncludeResolver* includeResolver = nullptr;
    MStringView customNamespace;
    Span<const MString> macroDefinitions;
    Span<const MString> cppHeaders;
    Span<const MString> cppIncludeDirectories;
    Span<const MString> cppDefines;
    Span<const MString> cppCompilerArgs;
    Span<const MString> extraPreambleHeaders;
    MString* outEmittedCppExterns = nullptr;
    b8 enableAutoProjectCppIncludes = true;
};

[[nodiscard]] b8 Compile(const CompileOptions& options, MetaArena& arena, CompileResult& outResult);
[[nodiscard]] inline b8 Compile(const MStringView source, const MStringView virtualFilePath, MetaArena& arena, CompileResult& outResult){
    CompileOptions options;
    options.source = source;
    options.virtualFilePath = virtualFilePath;
    return Compile(options, arena, outResult);
}

[[nodiscard]] MString DeriveOutputFilename(MStringView virtualFilePath, MStringView suffix, MetaArena& arena);
[[nodiscard]] MString DeriveNamespaceFromPath(MStringView virtualFilePath, MetaArena& arena);
[[nodiscard]] inline MString DeriveHeaderFilename(const MStringView virtualFilePath, MetaArena& arena){ return DeriveOutputFilename(virtualFilePath, ContractSemantics::s_GeneratedHeaderSuffix, arena); }
[[nodiscard]] inline MString DeriveSourceFilename(const MStringView virtualFilePath, MetaArena& arena){ return DeriveOutputFilename(virtualFilePath, ContractSemantics::s_GeneratedSourceSuffix, arena); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

