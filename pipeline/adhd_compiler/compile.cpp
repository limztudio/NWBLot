// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compile.h"
#include "output_transaction.h"

#include <core/common/log.h>

#include <global/filesystem.h>
#include <global/filesystem/path_identity.h>
#include <global/hash_utils.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_COMPILER_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_compile{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static b8 IsRepresentableDepfilePath(const AStringView path)noexcept{
    if(path.empty() || path.front() == '~' || path.back() == ':' || path.back() == '\\')
        return false;
    constexpr AStringView s_Punctuation = "+\"'&,/_:.~()}@!- #$\\{";
    for(usize index = 0u; index < path.size(); ++index){
        const char c = path[index];
        if(static_cast<u8>(c) < 0x80u && !IsAsciiAlphaNumeric(c) && s_Punctuation.find(c) == AStringView::npos)
            return false;
        if(index > 0u && path[index - 1u] == '\\' && (c == '#' || c == ':' || c == '$'))
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Core::Metascript::MString DeriveVirtualCompilePath(
    const Path<Core::Metascript::MetaArena>& inputPath,
    const Path<Core::Metascript::MetaArena>& resolvedInputPath,
    const Core::Metascript::MStringView rawInputView,
    const Span<const Core::Metascript::MString> includeDirs,
    Core::Metascript::MetaArena& arena
){
    using namespace Core::Metascript;

    MString genericInputStorage(arena);
    const MStringView genericView = GenericNativeTextView(inputPath.native(), genericInputStorage);
    MString resolvedGenericInputStorage(arena);
    const MStringView resolvedGenericView = GenericNativeTextView(resolvedInputPath.native(), resolvedGenericInputStorage);

    MStringView bestRelView;
    const auto considerCandidate = [&](const MStringView candidateRelView)noexcept{
        if(candidateRelView.empty())
            return;
        if(bestRelView.empty() || candidateRelView.size() < bestRelView.size())
            bestRelView = candidateRelView;
    };
    const auto tryPrefixMatch = [&](const MStringView fullView, MStringView dirGeneric)noexcept{
        while(dirGeneric.size() > 1u && dirGeneric.back() == '/')
            dirGeneric.remove_suffix(1u);
        if(dirGeneric.empty() || fullView.size() <= dirGeneric.size() + 1u)
            return;
        if(SameGenericPrefixText(fullView.substr(0u, dirGeneric.size()), dirGeneric) && fullView[dirGeneric.size()] == '/')
            considerCandidate(fullView.substr(dirGeneric.size() + 1u));
    };

    for(const MString& dirText : includeDirs){
        if(dirText.empty())
            continue;
        const Path<MetaArena> incPath(arena, dirText);
        MString genericIncStorage(arena);
        const MStringView genericInc = GenericNativeTextView(incPath.native(), genericIncStorage);
        tryPrefixMatch(genericView, genericInc);

        const auto resolvedInc = ResolveComparisonPath(incPath);
        if(!resolvedInc)
            continue;
        MString resolvedGenericIncStorage(arena);
        const MStringView resolvedGenericInc = GenericNativeTextView(resolvedInc->native(), resolvedGenericIncStorage);
        tryPrefixMatch(resolvedGenericView, resolvedGenericInc);

        Path<MetaArena> ancestor = resolvedInputPath;
        usize slashPos = resolvedGenericView.size();
        for(;;){
            const TStringView parentView = ancestor.parentPathView();
            if(parentView.empty() || parentView == ancestor.native())
                break;
            while(slashPos > 0u && resolvedGenericView[slashPos - 1u] != '/')
                --slashPos;
            if(slashPos == 0u)
                break;
            --slashPos;
            ancestor = Move(ancestor).parentPath();

            const auto sameDir = PathIdentity(ancestor, *resolvedInc).sameExisting();
            if(sameDir && *sameDir){
                considerCandidate(resolvedGenericView.substr(slashPos + 1u));
                break;
            }
        }
    }

    if(!bestRelView.empty())
        return MString(bestRelView, arena);
    return MString(rawInputView, arena);
}


struct NamespacePathHash{
    using is_transparent = void;

    [[nodiscard]] usize operator()(const Core::Metascript::MStringView view)const noexcept{
#if defined(FOUNDATION_PLATFORM_WINDOWS)
        return static_cast<usize>(UpdateFnv64TextCanonical(s_Fnv64OffsetBasis, view));
#else
        return TransparentStringHash<Core::Metascript::MString>{}(view);
#endif
    }
    [[nodiscard]] usize operator()(const Core::Metascript::MString& str)const noexcept{
        return (*this)(Core::Metascript::MStringView(str.data(), str.size()));
    }
};

struct NamespacePathEqual{
    using is_transparent = void;

    [[nodiscard]] b8 operator()(const Core::Metascript::MStringView lhs, const Core::Metascript::MStringView rhs)const noexcept{
        return SameGenericPrefixText(lhs, rhs);
    }
    [[nodiscard]] b8 operator()(const Core::Metascript::MString& lhs, const Core::Metascript::MString& rhs)const noexcept{
        return SameGenericPrefixText(lhs, rhs);
    }
    [[nodiscard]] b8 operator()(const Core::Metascript::MString& lhs, const Core::Metascript::MStringView rhs)const noexcept{
        return SameGenericPrefixText(lhs, rhs);
    }
    [[nodiscard]] b8 operator()(const Core::Metascript::MStringView lhs, const Core::Metascript::MString& rhs)const noexcept{
        return SameGenericPrefixText(lhs, rhs);
    }
};

using NamespaceMap = HashMap<
    Core::Metascript::MString,
    Core::Metascript::MString,
    Core::Metascript::MetaArena,
    NamespacePathHash,
    NamespacePathEqual
>;

[[nodiscard]] Core::Metascript::MString NormalizeNamespaceMapKey(const Core::Metascript::MStringView rawPath, Core::Metascript::MetaArena& arena){
    using namespace Core::Metascript;

    const Path<MetaArena> raw(arena, rawPath);
    Path<MetaArena> normalized(arena);
#if defined(FOUNDATION_PLATFORM_WINDOWS)
    const auto nativeText = raw.native();
    const b8 hasDriveAbs = raw.isAbsolute() || (nativeText.size() >= 3u && nativeText[1u] == FOUNDATION_TEXT(':') && (nativeText[2u] == FOUNDATION_TEXT('/') || nativeText[2u] == FOUNDATION_TEXT('\\')));
#else
    const b8 hasDriveAbs = raw.isAbsolute();
#endif
    if(hasDriveAbs){
        normalized = raw.lexicallyNormal();
    }
    else{
        const auto resolved = ResolveComparisonPath(raw);
        normalized = resolved ? *resolved : raw.lexicallyNormal();
    }
    return PathToGenericString<MChar>(arena, normalized);
}

[[nodiscard]] b8 LoadNamespaceMap(const Core::Metascript::MStringView mapPath, Core::Metascript::MetaArena& arena, NamespaceMap& outMap){
    using namespace Core::Metascript;

    MString content(arena);
    if(!ReadTextFile(Path<MetaArena>(arena, mapPath), content)){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("AdhdCompiler: failed to read namespace map '{}'"), ConvertString(mapPath));
        return false;
    }

    const MStringView text(content.data(), content.size());
    usize pos = 0u;
    while(pos < text.size()){
        usize lineEnd = text.find('\n', pos);
        if(lineEnd == MStringView::npos)
            lineEnd = text.size();
        MStringView line = text.substr(pos, lineEnd - pos);
        if(!line.empty() && line.back() == '\r')
            line = line.substr(0u, line.size() - 1u);
        pos = lineEnd + 1u;
        if(line.empty())
            continue;

        const usize tabPos = line.find('\t');
        if(tabPos == MStringView::npos){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("AdhdCompiler: invalid namespace map entry '{}' in '{}'"), ConvertString(line), ConvertString(mapPath));
            return false;
        }
        const MStringView pathPart = line.substr(0u, tabPos);
        const MStringView nsPart = line.substr(tabPos + 1u);
        if(!nsPart.empty() && !pathPart.empty()){
            MString key = NormalizeNamespaceMapKey(pathPart, arena);
            outMap.insert_or_assign(Move(key), MString(nsPart.data(), nsPart.size(), arena));
        }
    }
    return true;
}


class FilesystemIncludeResolver final : public Core::Metascript::IIncludeResolver{
private:
    struct ResolveResult{
        enum Enum : u8{
            Found,
            Missing,
            Error,
        };
    };


public:
    FilesystemIncludeResolver(
        const Path<Core::Metascript::MetaArena>& currentFileDir,
        const Span<const Core::Metascript::MString> includeDirs,
        const NamespaceMap* namespaceMap,
        Core::Metascript::MetaArena& arena
    )
        : m_resolvedParentDirs(arena)
        , m_includeDirs(includeDirs)
        , m_namespaceMap(namespaceMap)
    {
        m_resolvedParentDirs.push_back(currentFileDir);
    }


public:
    [[nodiscard]] virtual b8 resolveInclude(const Core::Metascript::MStringView includePath, const b8 isAngleBracket, Core::Metascript::MetaArena& arena, Core::Metascript::MString& outResolvedKey, Core::Metascript::MString& outContent)override{
        using namespace Core::Metascript;

        if(!Adhd::HasAdhdExtension(includePath))
            return false;
        const Path<MetaArena> include(arena, includePath);

        const auto tryResolveFromBase = [&](const Path<MetaArena>& baseDir) -> ResolveResult::Enum{
#if defined(FOUNDATION_PLATFORM_WINDOWS)
            const auto includeText = include.native();
            const b8 hasDrive = includeText.size() >= 2u && includeText[1u] == FOUNDATION_TEXT(':');
#else
            const b8 hasDrive = false;
#endif
            const auto candidate = ResolveComparisonPath(include.isAbsolute() || hasDrive ? include : baseDir / include);
            if(!candidate){
                NWB_LOGGER_ERROR(FOUNDATION_TEXT("AdhdCompiler: failed to resolve include '{}' from '{}': {}"), ConvertString(includePath), PathToString<tchar>(baseDir), ConvertString(candidate.error().message()));
                return ResolveResult::Error;
            }
            const auto directory = IsDirectory(*candidate);
            if(!directory || *directory){
                const ErrorCode error = directory ? MakeErrorCode(ErrorCondition::is_a_directory) : directory.error();
                if(IsMissingPathError(error))
                    return ResolveResult::Missing;
                NWB_LOGGER_ERROR(FOUNDATION_TEXT("AdhdCompiler: failed to read include '{}': {}"), PathToString<tchar>(*candidate), ConvertString(error.message()));
                return ResolveResult::Error;
            }
            const auto read = ReadTextFile(*candidate, outContent);
            if(!read){
                if(IsMissingPathError(read.error()))
                    return ResolveResult::Missing;
                NWB_LOGGER_ERROR(FOUNDATION_TEXT("AdhdCompiler: failed to read include '{}': {}"), PathToString<tchar>(*candidate), ConvertString(read.error().message()));
                return ResolveResult::Error;
            }
            outResolvedKey = PathToGenericString<MChar>(arena, *candidate);
            m_resolvedParentDirs.push_back(candidate->parentPath());
            return ResolveResult::Found;
        };

        if(!isAngleBracket){
            for(usize i = m_resolvedParentDirs.size(); i > 0u; --i){
                const ResolveResult::Enum result = tryResolveFromBase(m_resolvedParentDirs[i - 1u]);
                if(result != ResolveResult::Missing)
                    return result == ResolveResult::Found;
            }
        }

        for(const MString& dirText : m_includeDirs){
            const Path<MetaArena> baseDir(arena, dirText);
            const ResolveResult::Enum result = tryResolveFromBase(baseDir);
            if(result != ResolveResult::Missing)
                return result == ResolveResult::Found;
        }

        return false;
    }

    virtual void popIncludeScope()noexcept override{
        if(m_resolvedParentDirs.size() > 1u)
            m_resolvedParentDirs.pop_back();
    }
    [[nodiscard]] virtual Core::Metascript::MStringView resolveIncludeNamespace(const Core::Metascript::MStringView resolvedKey)const noexcept override{
        if(!m_namespaceMap || resolvedKey.empty())
            return {};
        const auto it = m_namespaceMap->find(resolvedKey);
        if(it == m_namespaceMap->end())
            return {};
        return Core::Metascript::MStringView(it.value().data(), it.value().size());
    }


private:
    Core::Metascript::MVector<Path<Core::Metascript::MetaArena>> m_resolvedParentDirs;
    Span<const Core::Metascript::MString> m_includeDirs;
    const NamespaceMap* m_namespaceMap = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 CompileAdhd(const AdhdCompileOptions& options){
    using namespace Core::Metascript;

    MetaArena& arena = options.arena;
    if(!options.emitCppExternsPath.empty() && options.inputs.size() > 1u){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("AdhdCompiler: --emit-cpp-externs requires a single input file"));
        return false;
    }

    const Path<MetaArena> outDir(arena, options.outputDirectory);
    MVector<Path<MetaArena>> inputPaths(arena);
    MVector<Path<MetaArena>> headerOutPaths(arena);
    MVector<Path<MetaArena>> sourceOutPaths(arena);
    MVector<Path<MetaArena>> outputPaths(arena);
    inputPaths.reserve(options.inputs.size());
    headerOutPaths.reserve(options.inputs.size());
    sourceOutPaths.reserve(options.inputs.size());
    outputPaths.reserve(options.inputs.size() * 2u + (options.emitCppExternsPath.empty() ? 0u : 1u) + (options.depfilePath.empty() ? 0u : 1u));
    for(const MString& input : options.inputs){
        Path<MetaArena> headerOutPath = outDir / Path<MetaArena>(arena, Adhd::Compiler::DeriveHeaderFilename(input, arena));
        Path<MetaArena> sourceOutPath = outDir / Path<MetaArena>(arena, Adhd::Compiler::DeriveSourceFilename(input, arena));
        auto absoluteInput = ResolveComparisonPath(Path<MetaArena>(arena, input));
        auto absoluteHeader = ResolveComparisonPath(headerOutPath);
        auto absoluteSource = ResolveComparisonPath(sourceOutPath);
        if(!absoluteInput){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("AdhdCompiler: failed to resolve input path '{}': {}"), ConvertString(input), ConvertString(absoluteInput.error().message()));
            return false;
        }
        if(!absoluteHeader){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("AdhdCompiler: failed to resolve header path for '{}': {}"), ConvertString(input), ConvertString(absoluteHeader.error().message()));
            return false;
        }
        if(!absoluteSource){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("AdhdCompiler: failed to resolve source path for '{}': {}"), ConvertString(input), ConvertString(absoluteSource.error().message()));
            return false;
        }
        inputPaths.push_back(Move(*absoluteInput));
        headerOutPaths.push_back(Move(headerOutPath));
        sourceOutPaths.push_back(Move(sourceOutPath));
        outputPaths.push_back(Move(*absoluteHeader));
        outputPaths.push_back(Move(*absoluteSource));
    }
    const auto conflictingPaths = [](const Path<MetaArena>& lhs, const Path<MetaArena>& rhs) -> b8{
        const auto same = PathIdentity(lhs, rhs).sameOutput();
        if(!same){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("AdhdCompiler: failed to compare '{}' and '{}': {}"), PathToString<tchar>(lhs), PathToString<tchar>(rhs), ConvertString(same.error().message()));
            return true;
        }
        if(*same){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("AdhdCompiler: paths '{}' and '{}' have the same output name"), PathToString<tchar>(lhs), PathToString<tchar>(rhs));
            return true;
        }
        return false;
    };
    for(usize i = 0u; i < outputPaths.size(); ++i){
        for(usize j = 0u; j < i; ++j){
            if(conflictingPaths(outputPaths[i], outputPaths[j]))
                return false;
        }
        for(const auto& input : inputPaths){
            if(conflictingPaths(outputPaths[i], input))
                return false;
        }
    }

    const auto registerOptionalOutput = [&](const MStringView rawPath, const tchar* kindLabel, Path<MetaArena>* outResolvedPath) -> b8{
        if(rawPath.empty())
            return true;
        auto resolved = ResolveComparisonPath(Path<MetaArena>(arena, rawPath));
        if(!resolved){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("AdhdCompiler: failed to resolve {} path '{}': {}"), kindLabel, ConvertString(rawPath), ConvertString(resolved.error().message()));
            return false;
        }
        for(const auto& input : inputPaths){
            if(conflictingPaths(*resolved, input))
                return false;
        }
        for(const auto& output : outputPaths){
            if(conflictingPaths(*resolved, output))
                return false;
        }
        if(outResolvedPath)
            *outResolvedPath = *resolved;
        outputPaths.push_back(Move(*resolved));
        return true;
    };

    Path<MetaArena> emitCppExternsOutPath(arena);
    Path<MetaArena> depfileOutPath(arena);
    if(!registerOptionalOutput(options.emitCppExternsPath, FOUNDATION_TEXT("externs output"), &emitCppExternsOutPath))
        return false;
    if(!registerOptionalOutput(options.depfilePath, FOUNDATION_TEXT("depfile"), &depfileOutPath))
        return false;

    const MStringView customNs = options.customNamespace;
    MString combinedDepContent(arena);
    MString emittedCppExterns(arena);
    MVector<MString> generatedHeaders(arena);
    MVector<MString> generatedSources(arena);
    generatedHeaders.reserve(options.inputs.size());
    generatedSources.reserve(options.inputs.size());

    __hidden_compile::NamespaceMap namespaceMap(0, __hidden_compile::NamespacePathHash(), __hidden_compile::NamespacePathEqual(), arena);
    const __hidden_compile::NamespaceMap* activeNamespaceMap = nullptr;
    if(!options.namespaceMapPath.empty()){
        if(!__hidden_compile::LoadNamespaceMap(options.namespaceMapPath, arena, namespaceMap))
            return false;
        activeNamespaceMap = &namespaceMap;
    }

    for(usize inputIndex = 0u; inputIndex < options.inputs.size(); ++inputIndex){
        const MString& inputPathText = options.inputs[inputIndex];
        const MStringView inputView = inputPathText;
        const Path<MetaArena> inputPath(arena, inputView);

        MString sourceContent(arena);
        if(!ReadTextFile(inputPath, sourceContent)){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("AdhdCompiler: failed to read input file '{}'"), ConvertString(inputView));
            return false;
        }

        const Path<MetaArena> parentDir = inputPaths[inputIndex].parentPath();
        __hidden_compile::FilesystemIncludeResolver resolver(parentDir, options.includeDirectories, activeNamespaceMap, arena);
        const MString virtualPath = __hidden_compile::DeriveVirtualCompilePath(inputPath, inputPaths[inputIndex], inputView, options.includeDirectories, arena);

        MVector<MString> cppIncludeDirs(arena);
        cppIncludeDirs.reserve(options.cppIncludeDirectories.size() + 1u);
        for(const MString& dir : options.cppIncludeDirectories)
            cppIncludeDirs.push_back(dir);
        MString inputParentText = PathToGenericString<MChar>(arena, parentDir);
        if(!inputParentText.empty())
            cppIncludeDirs.push_back(Move(inputParentText));

        Adhd::Compiler::CompileOptions compileOpts;
        compileOpts.source = sourceContent;
        compileOpts.virtualFilePath = virtualPath;
        compileOpts.includeResolver = &resolver;
        compileOpts.customNamespace = customNs;
        compileOpts.cppHeaders = options.cppHeaders;
        compileOpts.cppIncludeDirectories = cppIncludeDirs;
        compileOpts.cppDefines = options.cppDefines;
        compileOpts.cppCompilerArgs = options.cppCompilerArgs;
        if(!options.emitCppExternsPath.empty())
            compileOpts.outEmittedCppExterns = &emittedCppExterns;

        Adhd::CompileResult result(arena);
        if(!Adhd::Compiler::Compile(compileOpts, arena, result)){
            for(const ParseError& err : result.errors)
                NWB_LOGGER_ERROR(FOUNDATION_TEXT("{}:{}:{}: error: {}"), ConvertString(inputView), err.line, err.column, ConvertString(err.message));
            return false;
        }

        const Path<MetaArena>& headerOutPath = headerOutPaths[inputIndex];
        const Path<MetaArena>& sourceOutPath = sourceOutPaths[inputIndex];

        for(const MString& includePath : result.unit.resolvedIncludedFiles){
            const auto include = ResolveComparisonPath(Path<MetaArena>(arena, includePath));
            if(!include){
                NWB_LOGGER_ERROR(FOUNDATION_TEXT("AdhdCompiler: failed to resolve included path '{}': {}"), ConvertString(includePath), ConvertString(include.error().message()));
                return false;
            }
            for(const auto& output : outputPaths){
                if(conflictingPaths(*include, output))
                    return false;
            }
        }

        if(!options.depfilePath.empty()){
            MString headerGenericStorage(arena);
            MString sourceGenericStorage(arena);
            MString inputGenericStorage(arena);
            const MStringView headerGeneric = GenericNativeTextView(headerOutPath.native(), headerGenericStorage);
            const MStringView sourceGeneric = GenericNativeTextView(sourceOutPath.native(), sourceGenericStorage);
            const MStringView inputGeneric = GenericNativeTextView(inputPath.native(), inputGenericStorage);
            if(
                !__hidden_compile::IsRepresentableDepfilePath(headerGeneric)
                || !__hidden_compile::IsRepresentableDepfilePath(sourceGeneric)
                || !__hidden_compile::IsRepresentableDepfilePath(inputGeneric)
            ){
                NWB_LOGGER_ERROR(FOUNDATION_TEXT("AdhdCompiler: depfile path for '{}' cannot be represented safely"), ConvertString(inputView));
                return false;
            }
            for(const MString& depPath : result.unit.resolvedIncludedFiles){
                if(!__hidden_compile::IsRepresentableDepfilePath(depPath)){
                    NWB_LOGGER_ERROR(FOUNDATION_TEXT("AdhdCompiler: included dependency path for '{}' cannot be represented safely"), ConvertString(inputView));
                    return false;
                }
            }

            AppendDepfileEscapedPath(headerGeneric, combinedDepContent);
            combinedDepContent.push_back(' ');
            AppendDepfileEscapedPath(sourceGeneric, combinedDepContent);
            combinedDepContent.append(": ");
            AppendDepfileEscapedPath(inputGeneric, combinedDepContent);
            for(const MString& depPath : result.unit.resolvedIncludedFiles){
                combinedDepContent.push_back(' ');
                AppendDepfileEscapedPath(depPath, combinedDepContent);
            }
            combinedDepContent.push_back('\n');
        }
        generatedHeaders.push_back(Move(result.generatedHeader));
        generatedSources.push_back(Move(result.generatedSource));
    }

    if(!EnsureDirectories(outDir)){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("AdhdCompiler: failed to create output directory '{}'"), ConvertString(options.outputDirectory));
        return false;
    }
    const auto ensureOptionalParentDir = [](const Path<MetaArena>& targetPath, const MStringView rawPath, const tchar* kindLabel) -> b8{
        if(rawPath.empty())
            return true;
        const Path<MetaArena> parent = targetPath.parentPath();
        if(!parent.empty() && !EnsureDirectories(parent)){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("AdhdCompiler: failed to create {} directory for '{}'"), kindLabel, ConvertString(rawPath));
            return false;
        }
        return true;
    };
    if(!ensureOptionalParentDir(emitCppExternsOutPath, options.emitCppExternsPath, FOUNDATION_TEXT("output")))
        return false;
    if(!ensureOptionalParentDir(depfileOutPath, options.depfilePath, FOUNDATION_TEXT("depfile")))
        return false;

    const usize totalOutputs = outputPaths.size();
    MVector<MStringView> outputContents(arena);
    outputContents.reserve(totalOutputs);
    for(usize i = 0u; i < options.inputs.size(); ++i){
        outputContents.push_back(generatedHeaders[i]);
        outputContents.push_back(generatedSources[i]);
    }
    if(!options.emitCppExternsPath.empty())
        outputContents.push_back(emittedCppExterns);
    if(!options.depfilePath.empty())
        outputContents.push_back(combinedDepContent);

    AdhdOutputTransaction transaction(arena, outputPaths, outputContents, options.inputs, options.depfilePath, options.emitCppExternsPath);
    return transaction.publish();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_COMPILER_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

