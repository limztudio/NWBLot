// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compile.h"
#include "output_transaction.h"

#include <core/common/log.h>

#include <global/filesystem.h>
#include <global/filesystem/path_identity.h>
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

    const MString genericInput = PathToGenericString<MChar>(arena, inputPath);
    const MStringView genericView = genericInput;
    const MString resolvedGenericInput = PathToGenericString<MChar>(arena, resolvedInputPath);
    const MStringView resolvedGenericView = resolvedGenericInput;

    MStringView bestRelView;
    const auto considerCandidate = [&](const MStringView candidateRelView)noexcept{
        if(candidateRelView.empty())
            return;
        if(bestRelView.empty() || candidateRelView.size() < bestRelView.size())
            bestRelView = candidateRelView;
    };
    const auto tryPrefixMatch = [&](const MStringView fullView, MString& dirGeneric)noexcept{
        while(dirGeneric.size() > 1u && dirGeneric.back() == '/')
            dirGeneric.pop_back();
        if(dirGeneric.empty() || fullView.size() <= dirGeneric.size() + 1u)
            return;
        if(SameGenericPrefixText(fullView.substr(0u, dirGeneric.size()), dirGeneric) && fullView[dirGeneric.size()] == '/')
            considerCandidate(fullView.substr(dirGeneric.size() + 1u));
    };

    for(const MString& dirText : includeDirs){
        if(dirText.empty())
            continue;
        const Path<MetaArena> incPath(arena, dirText);
        MString genericInc = PathToGenericString<MChar>(arena, incPath);
        tryPrefixMatch(genericView, genericInc);

        const auto resolvedInc = ResolveComparisonPath(incPath);
        if(!resolvedInc)
            continue;
        MString resolvedGenericInc = PathToGenericString<MChar>(arena, *resolvedInc);
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
        usize h = static_cast<usize>(14695981039346656037ull);
        for(const Core::Metascript::MChar ch : view){
            h ^= static_cast<usize>(static_cast<u8>(Canonicalize(ch)));
            h *= static_cast<usize>(1099511628211ull);
        }
        return h;
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
        const MString relPath(includePath, arena);

        const auto tryResolveFromBase = [&](const Path<MetaArena>& baseDir) -> ResolveResult::Enum{
            const Path<MetaArena> include(arena, relPath);
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

    MetaArena& arena = options.inputs.get_allocator().arena();
    const Path<MetaArena> outDir(arena, options.outputDirectory);
    MVector<Path<MetaArena>> inputPaths(arena);
    MVector<Path<MetaArena>> outputPaths(arena);
    inputPaths.reserve(options.inputs.size());
    outputPaths.reserve(options.inputs.size() * 2u + (options.depfilePath.empty() ? 0u : 1u));
    for(const MString& input : options.inputs){
        auto absoluteInput = ResolveComparisonPath(Path<MetaArena>(arena, input));
        auto absoluteHeader = ResolveComparisonPath(outDir / Path<MetaArena>(arena, Adhd::Compiler::DeriveHeaderFilename(input, arena)));
        auto absoluteSource = ResolveComparisonPath(outDir / Path<MetaArena>(arena, Adhd::Compiler::DeriveSourceFilename(input, arena)));
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
    if(!options.depfilePath.empty()){
        auto depfile = ResolveComparisonPath(Path<MetaArena>(arena, options.depfilePath));
        if(!depfile){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("AdhdCompiler: failed to resolve depfile path '{}': {}"), ConvertString(options.depfilePath), ConvertString(depfile.error().message()));
            return false;
        }
        for(const auto& input : inputPaths){
            if(conflictingPaths(*depfile, input))
                return false;
        }
        for(const auto& output : outputPaths){
            if(conflictingPaths(*depfile, output))
                return false;
        }
        outputPaths.push_back(Move(*depfile));
    }

    const MStringView customNs = options.customNamespace;
    MString combinedDepContent(arena);
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

        Adhd::CompileResult result(arena);
        if(!Adhd::Compiler::Compile(sourceContent, virtualPath, &resolver, customNs, {}, arena, result)){
            for(const ParseError& err : result.errors)
                NWB_LOGGER_ERROR(FOUNDATION_TEXT("{}:{}:{}: error: {}"), ConvertString(inputView), err.line, err.column, ConvertString(err.message));
            return false;
        }

        const MString headerRel = Adhd::Compiler::DeriveHeaderFilename(inputView, arena);
        const MString sourceRel = Adhd::Compiler::DeriveSourceFilename(inputView, arena);

        const Path<MetaArena> headerOutPath = outDir / Path<MetaArena>(arena, headerRel);
        const Path<MetaArena> sourceOutPath = outDir / Path<MetaArena>(arena, sourceRel);

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
            const MString headerGeneric = PathToGenericString<MChar>(arena, headerOutPath);
            const MString sourceGeneric = PathToGenericString<MChar>(arena, sourceOutPath);
            const MString inputGeneric = PathToGenericString<MChar>(arena, inputPath);
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
    if(!options.depfilePath.empty()){
        const Path<MetaArena> depfileParent = outputPaths.back().parentPath();
        if(!depfileParent.empty() && !EnsureDirectories(depfileParent)){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("AdhdCompiler: failed to create depfile directory for '{}'"), ConvertString(options.depfilePath));
            return false;
        }
    }

    const usize totalOutputs = outputPaths.size();
    MVector<MStringView> outputContents(arena);
    outputContents.reserve(totalOutputs);
    for(usize i = 0u; i < options.inputs.size(); ++i){
        outputContents.push_back(generatedHeaders[i]);
        outputContents.push_back(generatedSources[i]);
    }
    if(!options.depfilePath.empty())
        outputContents.push_back(combinedDepContent);

    AdhdOutputTransaction transaction(arena, outputPaths, outputContents, options.inputs, options.depfilePath);
    return transaction.publish();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_COMPILER_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

