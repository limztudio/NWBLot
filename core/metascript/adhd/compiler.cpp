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


namespace __hidden_adhd_compiler{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void AppendPascalCaseSegment(MString& out, const MStringView segment){
    b8 capitalizeNext = true;
    b8 emittedAny = false;
    for(usize i = 0u; i < segment.size(); ++i){
        const MChar c = segment[i];
        const b8 isLower = IsAsciiLower(c);
        const b8 isUpper = IsAsciiUpper(c);
        const b8 isDigit = IsAsciiDigit(c);
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

[[nodiscard]] MString DeriveOutputFilename(const MStringView virtualFilePath, const MStringView suffix, MetaArena& arena){
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


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 Compiler::Compile(const MStringView source, const MStringView virtualFilePath, MetaArena& arena, CompileResult& outResult, IIncludeResolver* includeResolver, const MStringView customNamespace, const Span<const MString> macroDefinitions){
    Alloc::ScratchArena scratchArena(MetascriptArenaScope::s_ParserScratch);

    Preprocessor preprocessor(arena, scratchArena, outResult.errors, includeResolver);
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

    for(const MString& inc : preprocessor.includedFiles())
        outResult.unit.includedAdhdFiles.push_back(inc);
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

    const MString namespacePath = customNamespace.empty() ? DeriveNamespaceFromPath(virtualFilePath, arena) : MString(customNamespace.data(), customNamespace.size(), arena);
    const MString headerFilename = DeriveHeaderFilename(virtualFilePath, arena);

    HeaderEmitter headerEmitter(arena, outResult.symbols);
    SourceEmitter sourceEmitter(arena, outResult.symbols);

    outResult.generatedHeader = headerEmitter.emitHeader(outResult.unit, MStringView(namespacePath.data(), namespacePath.size()));
    outResult.generatedSource = sourceEmitter.emitSource(outResult.unit, MStringView(headerFilename.data(), headerFilename.size()), MStringView(namespacePath.data(), namespacePath.size()));

    return outResult.errors.empty();
}

MString Compiler::DeriveNamespaceFromPath(const MStringView virtualFilePath, MetaArena& arena){
    MString ns(arena);
    ns.append("NWB");

    if(virtualFilePath.empty() || virtualFilePath[0] == '/' || virtualFilePath[0] == '\\' || (virtualFilePath.size() >= 2u && virtualFilePath[1] == ':'))
        return ns;

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

MString Compiler::DeriveHeaderFilename(const MStringView virtualFilePath, MetaArena& arena){
    return __hidden_adhd_compiler::DeriveOutputFilename(virtualFilePath, ".adhd.h", arena);
}

MString Compiler::DeriveSourceFilename(const MStringView virtualFilePath, MetaArena& arena){
    return __hidden_adhd_compiler::DeriveOutputFilename(virtualFilePath, ".adhd.cpp", arena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

