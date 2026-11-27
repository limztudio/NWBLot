// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiler.h"

#include "cache_utils.h"
#include "clang/cpp_extern_trait_resolver.h"
#include "clang/cpp_header_importer.h"
#include "codegen/header_emitter.h"
#include "parser/decl_parser.h"
#include "sema/type_checker.h"
#include "sema/type_utils.h"

#include <core/metascript/arena_names.h>
#include <core/metascript/preprocessor_semantics.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


thread_local AstNodePool* AstNodePool::s_ActivePool = nullptr;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 Compiler::Compile(const CompileOptions& options, MetaArena& arena, CompileResult& outResult){
    const MStringView preludeError = BuiltinPreludeLoadError();
    if(!preludeError.empty()){
        outResult.errors.push_back(ParseError{1u, 1u, MString(preludeError.data(), preludeError.size(), arena)});
        return false;
    }

    Alloc::ScratchArena scratchArena(MetascriptArenaScope::s_ParserScratch);

    Preprocessor preprocessor(arena, scratchArena, outResult.errors, options.includeResolver, false);
    for(const MString& defText : options.macroDefinitions){
        const MStringView defView(defText.data(), defText.size());
        if(defView.empty())
            continue;
        const usize eqPos = defView.find('=');
        if(eqPos != MStringView::npos)
            preprocessor.defineObjectMacro(defView.substr(0u, eqPos), defView.substr(eqPos + 1u));
        else
            preprocessor.defineObjectMacro(defView, PreprocessorSemantics::s_DefaultDefinedMacroBody);
    }
    if(!preprocessor.process(options.source, outResult.preprocessedSource))
        return false;

    for(const MString& inc : preprocessor.includedFiles()){
        if(!HasAdhdExtension(MStringView(inc.data(), inc.size()))){
            outResult.errors.push_back(ParseError{1u, 1u, MString("#include in adhd may only include '.adhd' files; bind C++ headers via typed 'extern' declarations and separate '.h'/'.cpp' files", arena)});
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

    {
        MVector<MString> traitDefines(arena);
        MStringMap<u8> seenTraitDefines(arena);
        const auto addDefine = [&](const MString& def){
            if(!def.empty())
                CacheDetail::AddExistingUnique(traitDefines, seenTraitDefines, def);
        };
        for(const MString& def : options.cppDefines)
            addDefine(def);
        for(const MString& def : options.macroDefinitions)
            addDefine(def);

        MVector<MString> resolvedCppHeaders(arena);
        CppExternTraitResolveOptions traitOpts;
        traitOpts.includeDirectories = options.cppIncludeDirectories;
        traitOpts.defines = Span<const MString>(traitDefines.data(), traitDefines.size());
        traitOpts.compilerArgs = options.cppCompilerArgs;
        traitOpts.enableAutoProjectIncludes = options.enableAutoProjectCppIncludes;
        traitOpts.useDiskCache = true;
        traitOpts.outProjectIncludedFiles = &resolvedCppHeaders;
        CppExternTraitResolver::ResolveTranslationUnitTraits(traitOpts, arena, outResult.unit);

        if(!resolvedCppHeaders.empty()){
            MStringMap<u8> seenResolvedFiles(arena);
            for(const MString& existing : outResult.unit.resolvedIncludedFiles)
                seenResolvedFiles.emplace(existing, 1u);
            for(const MString& cppHdr : resolvedCppHeaders){
                const MStringView hdrView(cppHdr.data(), cppHdr.size());
                if(hdrView.empty() || hdrView.ends_with(ContractSemantics::s_GeneratedHeaderSuffix))
                    continue;
                CacheDetail::AddExistingUnique(outResult.unit.resolvedIncludedFiles, seenResolvedFiles, cppHdr);
            }
        }
    }

    for(const MString& extraHdr : options.extraPreambleHeaders){
        if(!extraHdr.empty())
            outResult.unit.extraPreambleHeaders.push_back(InternArenaString(arena, MStringView(extraHdr.data(), extraHdr.size())));
    }

    MVector<ImportedCppHeader> allCppHeaders(arena);
    for(const ImportedCppHeader& hdr : outResult.unit.importedCppHeaders)
        allCppHeaders.push_back(hdr);
    for(const MString& cliHdr : options.cppHeaders){
        if(cliHdr.empty())
            continue;
        ImportedCppHeader entry;
        entry.headerPath = MStringView(cliHdr.data(), cliHdr.size());
        entry.span = SourceSpan{ 1u, 1u };
        b8 isQuoted = false;
        const MStringView strippedPath = StripHeaderDelimiters(entry.headerPath, nullptr, &isQuoted);
        if(strippedPath.empty())
            continue;
        entry.isSystemHeader = !isQuoted;
        allCppHeaders.push_back(entry);
    }

    if(!allCppHeaders.empty()){
        CppHeaderImportOptions importOptions;
        importOptions.headers = Span<const ImportedCppHeader>(allCppHeaders.data(), allCppHeaders.size());
        importOptions.includeDirectories = options.cppIncludeDirectories;
        importOptions.defines = options.cppDefines;
        importOptions.compilerArgs = options.cppCompilerArgs;
        importOptions.enableAutoProjectIncludes = options.enableAutoProjectCppIncludes;

        if(!CppHeaderImporter::ImportHeaders(importOptions, arena, outResult.unit, outResult.errors))
            return false;
    }

    if(options.outEmittedCppExterns)
        *options.outEmittedCppExterns = CppHeaderImporter::FormatExternAdhd(outResult.unit, arena);

    const MString derivedNamespace = options.customNamespace.empty() ? DeriveNamespaceFromPath(options.virtualFilePath, arena) : MString(arena);
    const MStringView namespacePath = options.customNamespace.empty() ? MStringView(derivedNamespace.data(), derivedNamespace.size()) : options.customNamespace;
    outResult.unit.cppNamespace = InternArenaString(arena, namespacePath);

    TypeChecker typeChecker(arena, outResult.symbols);
    if(!typeChecker.checkTranslationUnit(outResult.unit, outResult.errors))
        return false;

    const MString headerFilename = DeriveHeaderFilename(options.virtualFilePath, arena);

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
    ns.append(ContractSemantics::s_DefaultRootNamespace.data(), ContractSemantics::s_DefaultRootNamespace.size());

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

    constexpr MStringView s_Sep = ContractSemantics::s_CppScopeSeparator;
    const MStringView dirPart = virtualFilePath.substr(0u, lastSlash);
    usize segStart = 0u;
    for(usize i = 0u; i <= dirPart.size(); ++i){
        if(i == dirPart.size() || GlobalFilesystemPathDetail::IsSeparator(dirPart[i])){
            if(i > segStart){
                const MStringView seg = dirPart.substr(segStart, i - segStart);
                if(seg != "." && seg != ".."){
                    const usize previousSize = ns.size();
                    ns.append(s_Sep.data(), s_Sep.size());
                    AppendPascalCaseIdentifierSegment(seg, ns);
                    if(ns.size() == previousSize + s_Sep.size())
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
        out.append(ContractSemantics::s_FallbackModuleStem.data(), ContractSemantics::s_FallbackModuleStem.size());
    else
        out.append(base.data(), base.size());
    out.append(suffix.data(), suffix.size());
    return out;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

