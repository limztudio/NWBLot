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
    b8 capitalizeNext = true;
    b8 emittedAny = false;
    for(usize i = 0u; i < segment.size(); ++i){
        const MChar c = segment[i];
        const b8 isLower = (c >= 'a' && c <= 'z');
        const b8 isUpper = (c >= 'A' && c <= 'Z');
        const b8 isDigit = (c >= '0' && c <= '9');
        if(!isLower && !isUpper && !isDigit){
            capitalizeNext = true;
            continue;
        }
        if(!emittedAny && isDigit)
            out.push_back('_');
        if(capitalizeNext && isLower){
            out.push_back(static_cast<MChar>(c - ('a' - 'A')));
            capitalizeNext = false;
        }
        else{
            out.push_back(c);
            capitalizeNext = false;
        }
        emittedAny = true;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 Compiler::compile(
    const MStringView source,
    const MStringView virtualFilePath,
    MetaArena& arena,
    CompileResult& outResult,
    IIncludeResolver* includeResolver,
    const MStringView customNamespace,
    const MVector<MString>* macroDefinitions
){
    Alloc::ScratchArena scratchArena(MetascriptArenaScope::s_ParserScratch);

    Preprocessor preprocessor(arena, scratchArena, outResult.errors, includeResolver);
    if(macroDefinitions){
        for(const MString& defText : *macroDefinitions){
            const MStringView defView(defText.data(), defText.size());
            if(defView.empty())
                continue;
            usize eqPos = defView.size();
            for(usize i = 0u; i < defView.size(); ++i){
                if(defView[i] == '='){
                    eqPos = i;
                    break;
                }
            }
            if(eqPos < defView.size())
                preprocessor.defineObjectMacro(defView.substr(0u, eqPos), defView.substr(eqPos + 1u));
            else
                preprocessor.defineObjectMacro(defView, "1");
        }
    }
    if(!preprocessor.process(source, outResult.preprocessedSource))
        return false;

    for(const MString& inc : preprocessor.includedFiles())
        outResult.unit.includedAdhdFiles.push_back(inc);

    const MStringView activeSource(outResult.preprocessedSource.data(), outResult.preprocessedSource.size());
    ParserCursor cursor(activeSource, arena, scratchArena, outResult.errors);
    DeclParser declParser(cursor, activeSource, &preprocessor);

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
    SourceEmitter sourceEmitter(arena, outResult.symbols);

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

    if(virtualFilePath.empty()
        || virtualFilePath[0] == '/'
        || virtualFilePath[0] == '\\'
        || (virtualFilePath.size() >= 2u && virtualFilePath[1] == ':'))
    {
        return ns;
    }

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
                    MString segPascal(arena);
                    __hidden_adhd_compiler::AppendPascalCaseSegment(segPascal, seg);
                    if(!segPascal.empty()){
                        ns.append("::");
                        ns.append(segPascal.data(), segPascal.size());
                    }
                }
            }
            segStart = i + 1u;
        }
    }

    return ns;
}

MString Compiler::deriveHeaderFilename(const MStringView virtualFilePath, MetaArena& arena){
    const MStringView base = ExtractAdhdFileStem(virtualFilePath);

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

