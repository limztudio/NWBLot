// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/metascript/preprocessor.h>

#include "codegen/header_emitter.h"
#include "codegen/source_emitter.h"
#include "parser/decl_parser.h"
#include "sema/type_checker.h"


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


class Compiler final{
public:
    [[nodiscard]] static b8 Compile(MStringView source, MStringView virtualFilePath, MetaArena& arena, CompileResult& outResult, IIncludeResolver* includeResolver = nullptr, MStringView customNamespace = {}, Span<const MString> macroDefinitions = {});

    [[nodiscard]] static MString DeriveNamespaceFromPath(MStringView virtualFilePath, MetaArena& arena);
    [[nodiscard]] static MString DeriveHeaderFilename(MStringView virtualFilePath, MetaArena& arena);
    [[nodiscard]] static MString DeriveSourceFilename(MStringView virtualFilePath, MetaArena& arena);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

