// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiler.h"

#include <core/metascript/arena_names.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_compiler{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void AppendPascalCaseSegment(MString& out, const MStringView segment){
    bool capitalizeNext = true;
    for(usize i = 0u; i < segment.size(); ++i){
        const MChar c = segment[i];
        if(c == '_' || c == '-'){
            capitalizeNext = true;
            continue;
        }
        if(capitalizeNext && c >= 'a' && c <= 'z'){
            out.push_back(static_cast<MChar>(c - ('a' - 'A')));
            capitalizeNext = false;
        }
        else{
            out.push_back(c);
            capitalizeNext = false;
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Compiler::compile(
    const MStringView source,
    const MStringView virtualFilePath,
    MetaArena& arena,
    CompileResult& outResult,
    IIncludeResolver* includeResolver,
    const MStringView customNamespace
){
    Alloc::ScratchArena scratchArena(MetascriptArenaScope::s_ParserScratch);

    PreprocessorOptions ppOpts;
    ppOpts.includeResolver = includeResolver;
    ppOpts.preserveNativeIncludeDirectives = true;
    ppOpts.expandIncludedFilesInline = (includeResolver != nullptr);

    Preprocessor preprocessor(arena, scratchArena, outResult.errors, ppOpts);
    if(!preprocessor.process(source, outResult.preprocessedSource))
        return false;

    for(const MString& inc : preprocessor.includedFiles())
        outResult.unit.includedAdhdFiles.push_back(inc);

    const MStringView activeSource(outResult.preprocessedSource.data(), outResult.preprocessedSource.size());
    ParserCursor cursor(activeSource, arena, scratchArena, outResult.errors, true);
    DeclParser declParser(cursor, activeSource);

    if(!declParser.parseTranslationUnit(outResult.unit))
        return false;

    TypeChecker typeChecker(arena, outResult.symbols);
    if(!typeChecker.checkTranslationUnit(outResult.unit, outResult.errors))
        return false;

    const MString namespacePath = customNamespace.empty()
        ? deriveNamespaceFromPath(virtualFilePath, arena)
        : MString(customNamespace.data(), customNamespace.size(), arena);
    const MString headerFilename = deriveHeaderFilename(virtualFilePath, arena);

    HeaderEmitter headerEmitter(arena, outResult.symbols);
    SourceEmitter sourceEmitter(arena, outResult.symbols, headerEmitter);

    outResult.generatedHeader = headerEmitter.emitHeader(
        outResult.unit,
        MStringView(namespacePath.data(), namespacePath.size())
    );
    outResult.generatedSource = sourceEmitter.emitSource(
        outResult.unit,
        MStringView(headerFilename.data(), headerFilename.size()),
        MStringView(namespacePath.data(), namespacePath.size())
    );

    return outResult.errors.empty();
}

MString Compiler::deriveNamespaceFromPath(const MStringView virtualFilePath, MetaArena& arena){
    MString ns(arena);
    ns.append("NWB");

    usize lastSlash = virtualFilePath.size();
    for(usize i = 0u; i < virtualFilePath.size(); ++i){
        if(virtualFilePath[i] == '/' || virtualFilePath[i] == '\\')
            lastSlash = i;
    }

    if(lastSlash == virtualFilePath.size() || lastSlash == 0u)
        return ns;

    const MStringView dirPart = virtualFilePath.substr(0u, lastSlash);
    usize segStart = 0u;
    for(usize i = 0u; i <= dirPart.size(); ++i){
        if(i == dirPart.size() || dirPart[i] == '/' || dirPart[i] == '\\'){
            if(i > segStart){
                const MStringView seg = dirPart.substr(segStart, i - segStart);
                if(seg != "." && seg != ".."){
                    ns.append("::");
                    __hidden_adhd_compiler::AppendPascalCaseSegment(ns, seg);
                }
            }
            segStart = i + 1u;
        }
    }

    return ns;
}

MString Compiler::deriveHeaderFilename(const MStringView virtualFilePath, MetaArena& arena){
    usize fileStart = 0u;
    for(usize i = 0u; i < virtualFilePath.size(); ++i){
        if(virtualFilePath[i] == '/' || virtualFilePath[i] == '\\')
            fileStart = i + 1u;
    }

    MStringView base = virtualFilePath.substr(fileStart);
    if(base.size() > 5u && base.substr(base.size() - 5u) == ".adhd")
        base = base.substr(0u, base.size() - 5u);

    MString out(arena);
    if(base.empty())
        out.append("module");
    else
        out.append(base.data(), base.size());
    out.append(".adhd.h");
    return out;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

