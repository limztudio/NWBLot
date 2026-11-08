// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "slang_compiler_workspace.h"
#include "source_dependencies.h"

#include <core/common/log.h>

#include <global/filesystem/cleanup.h>
#include <global/hash_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Alloc = Core::Alloc;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_slang_compiler_workspace{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace ShaderBinaryPayload;

Atomic<u64> g_TemporarySequence{0u};

struct CompilerIncludeReplacement{
    SlangCompilerWorkspace::ScratchString pathText;
    usize sourceOffset;
    usize sourceBytes;
};

[[nodiscard]] b8 WriteBomStrippedCompilerInput(
    const Path& sourcePath,
    const ShaderCook::ShaderCompilerRequest& request,
    const SlangCompilerWorkspace::ScratchPath& overlayRoot,
    const SlangCompilerWorkspace::ScratchPath& outputPath,
    SlangCompilerWorkspace::ScratchString& source,
    Alloc::ScratchArena& scratchArena
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SlangCompilerWorkspace::ScopedDirectoryCleanupGuard::~ScopedDirectoryCleanupGuard()noexcept{
    const auto removed = RemoveAllForCleanup(m_path);
    if(removed)
        return;

    const DiagnosticEventText diagnostic = MakeDiagnosticEventText("ShaderCook: failed to remove temporary compiler directory '{}', error {}", m_path.c_str(), removed.error().value());
    CaptureDiagnosticEvent(DiagnosticEventRecord{
        .event = ShaderBinaryPayload::s_SlangWorkspaceDiagnosticWarningEventName,
        .category = ShaderBinaryPayload::s_SlangWorkspaceCleanupDiagnosticCategory,
        .message = diagnostic.view(),
        .file = __FILE__,
        .line = __LINE__,
    });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<SlangCompilerWorkspace::ScratchPath> SlangCompilerWorkspace::CreateCompilerWorkDirectory(const ScratchPath& parentDirectory, Alloc::ScratchArena& scratchArena){
    auto ensureDirectoriesResult = EnsureDirectories(parentDirectory);
    if(!ensureDirectoriesResult){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to create compiler temporary parent '{}' : {}"), PathToString<tchar>(parentDirectory), ConvertString(ensureDirectoriesResult.error().message()));
        return MakeUnexpected(Failure{});
    }

    for(;;){
        const u64 sequence = __hidden_slang_compiler_workspace::g_TemporarySequence.fetch_add(1u, MemoryOrder::relaxed);
        if(sequence == Limit<u64>::s_Max){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: compiler temporary sequence overflow"));
            return MakeUnexpected(Failure{});
        }
        ScratchString directoryName(ShaderBinaryPayload::s_ShaderWorkspacePrefix, scratchArena);
        directoryName.reserve(directoryName.size() + ShaderBinaryPayload::s_ShaderWorkspaceHexPidAndSeqReserveBytes);
        AppendHexU64(CurrentProcessId(), directoryName);
        directoryName += '_';
        AppendHexU64(sequence, directoryName);
        ScratchPath directory = parentDirectory / directoryName;
        const auto created = CreateDirectories(directory);
        if(created && *created)
            return directory;
        if(!created){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to create isolated compiler directory '{}' : {}"), PathToString<tchar>(directory), ConvertString(created.error().message()));
            return MakeUnexpected(Failure{});
        }
    }
}

Expected<usize> SlangCompilerWorkspace::CompilerPathTextCapacity(const ScratchPath& path){
    return ShaderBinaryPayload::TryComputeCompilerPathTextCapacity(path.native().size(), IsSame_V<ScratchPath::value_type, char>);
}

Expected<SlangCompilerWorkspace::ScratchString> SlangCompilerWorkspace::BuildCompilerPathText(const ScratchPath& path, Alloc::ScratchArena& scratchArena){
    const auto capacity = CompilerPathTextCapacity(path);
    if(!capacity)
        return MakeUnexpected(Failure{});
    ScratchString text{scratchArena};
    text.reserve(*capacity);
    auto out = BackInserter(text);
    WriteConvertedText<char>(out, path.native());
    return text;
}

Expected<SlangCompilerWorkspace::ScratchPath> SlangCompilerWorkspace::BuildCompilerOverlayPath(const ScratchPath& overlayRoot, const ScratchPath& absolutePath, Alloc::ScratchArena& scratchArena){
    if(!absolutePath.isAbsolute())
        return MakeUnexpected(Failure{});

    const auto rootIt = absolutePath.begin();
    if(rootIt == absolutePath.end())
        return MakeUnexpected(Failure{});

    const ScratchPath rootPath(scratchArena, rootIt.nativeComponent());
    const ScratchPath relativePath = absolutePath.lexicallyRelative(rootPath);
    if(relativePath.empty())
        return MakeUnexpected(Failure{});

    const auto rootText = BuildCompilerPathText(rootPath, scratchArena);
    if(!rootText)
        return MakeUnexpected(Failure{});
    const auto rootKeyCapacity = ShaderBinaryPayload::TryComputeCompilerOverlayRootKeyCapacity(rootText->size());
    if(!rootKeyCapacity)
        return MakeUnexpected(Failure{});
    ScratchString rootKey(ShaderBinaryPayload::s_SlangWorkspaceRootKeyPrefix, scratchArena);
    rootKey.reserve(*rootKeyCapacity);
    for(const char ch : *rootText){
        const u8 byte = static_cast<u8>(ch);
        rootKey += ShaderBinaryPayload::s_SlangWorkspaceHexDigits[byte >> s_HexNibbleBits];
        rootKey += ShaderBinaryPayload::s_SlangWorkspaceHexDigits[byte & s_HexNibbleMask];
    }
    return overlayRoot / rootKey / relativePath;
}

b8 SlangCompilerWorkspace::IsCompilerDependency(const Span<const Path> dependencies, const ScratchPath& absolutePath)noexcept{
    for(const Path& dependency : dependencies){
        if(dependency == absolutePath)
            return true;
    }
    return false;
}

Expected<SlangCompilerWorkspace::CompilerInputs> SlangCompilerWorkspace::PrepareBomStrippedCompilerInputs(const ShaderCook::ShaderCompilerRequest& request, const ScratchPath& overlayRoot, Alloc::ScratchArena& scratchArena){
    ScratchVector<ScratchPath> includeDirectories(scratchArena);

    if(request.dependencies.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: compiler request for '{}' has no resolved source dependencies"), ConvertString(request.shaderName));
        return MakeUnexpected(Failure{});
    }

    if(!request.compilerInputsHaveBom){
        includeDirectories.reserve(request.includeDirectories.size());
        for(const Path& includeDirectory : request.includeDirectories)
            includeDirectories.emplace_back(scratchArena, includeDirectory);
        return CompilerInputs{ .includeDirectories = Move(includeDirectories), .sourcePath = ScratchPath(scratchArena, request.sourcePath) };
    }

    auto ensureDirectoriesResult2 = EnsureDirectories(overlayRoot);
    if(!ensureDirectoriesResult2){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to create temporary compiler source directory '{}' : {}"), PathToString<tchar>(overlayRoot), ConvertString(ensureDirectoriesResult2.error().message()));
        return MakeUnexpected(Failure{});
    }

    // Encode each absolute root separately and preserve nested relative includes within that root.
    for(const Path& dependency : request.dependencies){
        const auto overlayDependencyPath = BuildCompilerOverlayPath(overlayRoot, ScratchPath(scratchArena, dependency), scratchArena);
        if(!overlayDependencyPath){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to map compiler input '{}' into the temporary source directory"), PathToString<tchar>(dependency));
            return MakeUnexpected(Failure{});
        }

        auto ensureDirectoriesResult3 = EnsureDirectories(overlayDependencyPath->parentPath());
        if(!ensureDirectoriesResult3){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to create temporary compiler source parent '{}' : {}"), PathToString<tchar>(overlayDependencyPath->parentPath()), ConvertString(ensureDirectoriesResult3.error().message()));
            return MakeUnexpected(Failure{});
        }

        ScratchString sourceText{scratchArena};
        const auto sourceRead = ReadTextFile(dependency, sourceText);
        if(!sourceRead){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to read compiler input '{}' while writing BOM-stripped source: {}:{}"), PathToString<tchar>(dependency), ConvertString(sourceRead.error().category().name()), sourceRead.error().value());
            return MakeUnexpected(Failure{});
        }
        StripUtf8Bom(sourceText);
        if(!__hidden_slang_compiler_workspace::WriteBomStrippedCompilerInput(dependency, request, overlayRoot, *overlayDependencyPath, sourceText, scratchArena))
            return MakeUnexpected(Failure{});
    }

    const auto absoluteSourcePath = AbsolutePath(ScratchPath(scratchArena, request.sourcePath));
    if(!absoluteSourcePath){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to resolve compiler source '{}': {}:{}"), PathToString<tchar>(request.sourcePath), ConvertString(absoluteSourcePath.error().category().name()), absoluteSourcePath.error().value());
        return MakeUnexpected(Failure{});
    }
    const ScratchPath normalizedSourcePath = absoluteSourcePath->lexicallyNormal();
    auto sourcePathResult = BuildCompilerOverlayPath(overlayRoot, normalizedSourcePath, scratchArena);
    if(!IsCompilerDependency(request.dependencies, normalizedSourcePath) || !sourcePathResult){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to map source '{}' into the temporary compiler source directory"), PathToString<tchar>(request.sourcePath));
        return MakeUnexpected(Failure{});
    }

    const auto includeDirectoryCapacity = ShaderBinaryPayload::TryComputeCompilerIncludeDirectoryCapacity(request.shaderName, request.includeDirectories.size());
    if(!includeDirectoryCapacity)
        return MakeUnexpected(Failure{});

    includeDirectories.reserve(*includeDirectoryCapacity);
    for(const Path& includeDirectory : request.includeDirectories){
        const auto absoluteIncludeDirectory = AbsolutePath(ScratchPath(scratchArena, includeDirectory));
        if(!absoluteIncludeDirectory){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to resolve compiler include directory '{}': {}:{}"), PathToString<tchar>(includeDirectory), ConvertString(absoluteIncludeDirectory.error().category().name()), absoluteIncludeDirectory.error().value());
            return MakeUnexpected(Failure{});
        }
        const ScratchPath normalizedIncludeDirectory = absoluteIncludeDirectory->lexicallyNormal();
        auto overlayIncludeDirectory = BuildCompilerOverlayPath(overlayRoot, normalizedIncludeDirectory, scratchArena);
        if(!overlayIncludeDirectory){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to map include directory '{}' into the temporary compiler source directory"), PathToString<tchar>(includeDirectory));
            return MakeUnexpected(Failure{});
        }
        includeDirectories.push_back(Move(*overlayIncludeDirectory));
    }
    for(const Path& includeDirectory : request.includeDirectories)
        includeDirectories.emplace_back(scratchArena, includeDirectory);

    return CompilerInputs{ .includeDirectories = Move(includeDirectories), .sourcePath = Move(*sourcePathResult) };
}

b8 __hidden_slang_compiler_workspace::WriteBomStrippedCompilerInput(
    const Path& sourcePath,
    const ShaderCook::ShaderCompilerRequest& request,
    const SlangCompilerWorkspace::ScratchPath& overlayRoot,
    const SlangCompilerWorkspace::ScratchPath& outputPath,
    SlangCompilerWorkspace::ScratchString& source,
    Alloc::ScratchArena& scratchArena
){
    using namespace SlangCompilerWorkspace;
    SpliceSourceLines(source);
    ScratchString scanSource(AStringView(source), scratchArena);
    MaskCStyleSourceComments(scanSource);
    const AStringView sourceView(source.data(), source.size());
    const AStringView scanView(scanSource);
    usize includeCount = 0u;
    for(usize begin = 0u; begin < scanView.size();){
        const usize foundEnd = scanView.find('\n', begin);
        const usize end = foundEnd == AStringView::npos ? scanView.size() : foundEnd;
        if(ShaderBinaryPayload::TryExtractIncludeDirective(scanView.substr(begin, end - begin)).has_value())
            ++includeCount;
        begin = end < scanView.size() ? end + 1u : end;
    }
    ScratchVector<__hidden_slang_compiler_workspace::CompilerIncludeReplacement> replacements{scratchArena};
    replacements.reserve(includeCount);
    usize outputBytes = sourceView.size();
    for(usize begin = 0u; begin < scanView.size();){
        const usize foundEnd = scanView.find('\n', begin);
        const usize end = foundEnd == AStringView::npos ? scanView.size() : foundEnd;
        const AStringView scanLine = scanView.substr(begin, end - begin);
        const Expected<ShaderBinaryPayload::IncludeDirective> include = ShaderBinaryPayload::TryExtractIncludeDirective(scanLine);
        if(include && include->kind == ShaderBinaryPayload::IncludeKind::Unsupported){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: unsupported dependency directive '{}' in '{}'"), ConvertString(TrimView(scanLine)), PathToString<tchar>(sourcePath));
            return false;
        }
        if(include && include->kind != ShaderBinaryPayload::IncludeKind::Macro){
            const ScratchPath includePath(scratchArena, include->name);
            if(includePath.isAbsolute()){
                const auto absoluteIncludePath = AbsolutePath(includePath);
                if(!absoluteIncludePath){
                    NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to resolve compiler include '{}': {}:{}"), PathToString<tchar>(includePath), ConvertString(absoluteIncludePath.error().category().name()), absoluteIncludePath.error().value());
                    return false;
                }
                const ScratchPath normalizedIncludePath = absoluteIncludePath->lexicallyNormal();
                auto overlayIncludePath = BuildCompilerOverlayPath(overlayRoot, normalizedIncludePath, scratchArena);
                if(!IsCompilerDependency(request.dependencies, normalizedIncludePath) || !overlayIncludePath){
                    NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: absolute include '{}' from '{}' is not a mapped compiler dependency"), ConvertString(include->name), PathToString<tchar>(sourcePath));
                    return false;
                }
                auto pathText = BuildCompilerPathText(*overlayIncludePath, scratchArena);
                if(!pathText)
                    return false;
                const auto replacementOutputBytes = ShaderBinaryPayload::TryComputeCompilerReplacementOutputBytes(outputBytes, include->name.size(), pathText->size());
                if(!replacementOutputBytes){
                    NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: BOM-stripped compiler input '{}' exceeds representable size"), PathToString<tchar>(outputPath));
                    return false;
                }
                outputBytes = *replacementOutputBytes;
                const usize sourceOffset = begin + static_cast<usize>(include->name.data() - scanLine.data());
                replacements.push_back({ Move(*pathText), sourceOffset, include->name.size() });
            }
        }
        begin = end < scanView.size() ? end + 1u : end;
    }
    if(!CanRepresentStreamSize(static_cast<u64>(outputBytes))){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: BOM-stripped compiler input '{}' exceeds stream size"), PathToString<tchar>(outputPath));
        return false;
    }

    // Stream borrowed slices so replacement output never blocks the source/scan buffers' LIFO retirement.
    OutputFileStream stream(outputPath, s_FileOpenBinary | s_FileOpenTruncate);
    if(!stream.is_open()){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to open BOM-stripped compiler input '{}' for writing"), PathToString<tchar>(outputPath));
        return false;
    }
    usize sourceCursor = 0u;
    for(const __hidden_slang_compiler_workspace::CompilerIncludeReplacement& replacement : replacements){
        if(replacement.sourceOffset > sourceCursor)
            stream.write(sourceView.data() + sourceCursor, static_cast<StreamSize>(replacement.sourceOffset - sourceCursor));
        if(!replacement.pathText.empty())
            stream.write(replacement.pathText.data(), static_cast<StreamSize>(replacement.pathText.size()));
        sourceCursor = replacement.sourceOffset + replacement.sourceBytes;
    }
    if(sourceCursor < sourceView.size())
        stream.write(sourceView.data() + sourceCursor, static_cast<StreamSize>(sourceView.size() - sourceCursor));
    stream.close();
    if(!stream.good()){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to write or close BOM-stripped compiler input '{}'"), PathToString<tchar>(outputPath));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

