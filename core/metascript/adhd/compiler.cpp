// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiler.h"

#include "codegen/header_emitter.h"
#include "parser/decl_parser.h"
#include "sema/type_checker.h"

#include <core/metascript/arena_names.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 Compiler::Compile(const MStringView source, const MStringView virtualFilePath, IIncludeResolver* includeResolver, const MStringView customNamespace, const Span<const MString> macroDefinitions, MetaArena& arena, CompileResult& outResult){
    Alloc::ScratchArena scratchArena(MetascriptArenaScope::s_ParserScratch);

    Preprocessor preprocessor(arena, scratchArena, outResult.errors, includeResolver, false);
    for(const MString& defText : macroDefinitions){
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
    if(!preprocessor.process(source, outResult.preprocessedSource))
        return false;

    for(const MString& inc : preprocessor.includedFiles()){
        if(!HasAdhdExtension(MStringView(inc.data(), inc.size()))){
            outResult.errors.push_back(ParseError{1u, 1u, MString("outside 'native { ... }', '#include' in adhd may only include '.adhd' files; place C++ '#include' directives inside a 'native' block", arena)});
            return false;
        }
        outResult.unit.includedAdhdFiles.push_back(inc);
    }
    for(const MString& resInc : preprocessor.resolvedIncludedFiles())
        outResult.unit.resolvedIncludedFiles.push_back(resInc);

    const MStringView activeSource(outResult.preprocessedSource.data(), outResult.preprocessedSource.size());
    ParserCursor cursor(activeSource, arena, scratchArena, outResult.errors);
    DeclParser declParser(cursor, activeSource, &preprocessor);

    if(!declParser.parseTranslationUnit(outResult.unit))
        return false;

    TypeChecker typeChecker(arena, outResult.symbols);
    if(!typeChecker.checkTranslationUnit(outResult.unit, outResult.errors))
        return false;

    const MString derivedNamespace = customNamespace.empty() ? DeriveNamespaceFromPath(virtualFilePath, arena) : MString(arena);
    const MStringView namespacePath = customNamespace.empty() ? MStringView(derivedNamespace.data(), derivedNamespace.size()) : customNamespace;
    const MString headerFilename = DeriveHeaderFilename(virtualFilePath, arena);

    HeaderEmitter headerEmitter(arena, outResult.symbols);
    SourceEmitter sourceEmitter(arena, outResult.symbols);

    MString generatedHeader = headerEmitter.emitHeader(outResult.unit, namespacePath);
    MString generatedSource = sourceEmitter.emitSource(outResult.unit, MStringView(headerFilename.data(), headerFilename.size()), namespacePath);
    outResult.generatedHeader = Move(generatedHeader);
    outResult.generatedSource = Move(generatedSource);

    return outResult.errors.empty();
}

MString Compiler::DeriveNamespaceFromPath(const MStringView virtualFilePath, MetaArena& arena){
    MString ns(arena);
    ns.append("NWB");

    if(
        virtualFilePath.empty()
        || GlobalFilesystemPathDetail::IsSeparator(virtualFilePath[0])
        || GlobalFilesystemPathDetail::RootNameLength(virtualFilePath) != 0u
    )
        return ns;

    usize lastSlash = virtualFilePath.size();
    for(usize i = 0u; i < virtualFilePath.size(); ++i){
        if(GlobalFilesystemPathDetail::IsSeparator(virtualFilePath[i]))
            lastSlash = i;
    }

    if(lastSlash == virtualFilePath.size() || lastSlash == 0u)
        return ns;

    const MStringView dirPart = virtualFilePath.substr(0u, lastSlash);
    usize segStart = 0u;
    for(usize i = 0u; i <= dirPart.size(); ++i){
        if(i == dirPart.size() || GlobalFilesystemPathDetail::IsSeparator(dirPart[i])){
            if(i > segStart){
                const MStringView seg = dirPart.substr(segStart, i - segStart);
                if(seg != "." && seg != ".."){
                    const usize previousSize = ns.size();
                    ns.append("::");
                    AppendPascalCaseIdentifierSegment(seg, ns);
                    if(ns.size() == previousSize + 2u)
                        ns.erase(previousSize);
                }
            }
            segStart = i + 1u;
        }
    }

    return ns;
}

MString Compiler::DeriveOutputFilename(const MStringView virtualFilePath, const MStringView suffix, MetaArena& arena){
    const MStringView base = ExtractAdhdFileStem(virtualFilePath);
    MString out(arena);
    if(base.empty())
        out.append("module");
    else
        out.append(base.data(), base.size());
    out.append(suffix.data(), suffix.size());
    return out;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

