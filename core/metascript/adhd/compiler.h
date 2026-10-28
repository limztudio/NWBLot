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


class Compiler{
public:
    [[nodiscard]] static bool compile(
        MStringView source,
        MStringView virtualFilePath,
        MetaArena& arena,
        CompileResult& outResult,
        IIncludeResolver* includeResolver = nullptr,
        MStringView customNamespace = {},
        const MVector<MString>* macroDefinitions = nullptr
    );

    [[nodiscard]] static MString deriveNamespaceFromPath(MStringView virtualFilePath, MetaArena& arena);
    [[nodiscard]] static MString deriveHeaderFilename(MStringView virtualFilePath, MetaArena& arena);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

