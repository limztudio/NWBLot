// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "slang_compiler_workspace.h"
#include "source_dependencies.h"

#include <core/common/log.h>

#include <global/filesystem/cleanup.h>
#include <global/hash_utils.h>
#include <global/process_execution.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Alloc = Core::Alloc;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_slang_compiler_workspace{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_RootKeyPrefix = "root_";
static constexpr AStringView s_DiagnosticWarningEventName = "warning";

Atomic<u64> g_TemporarySequence{0u};

struct CompilerIncludeReplacement{
    SlangCompilerWorkspace::ScratchPath path;
    usize sourceOffset;
    usize sourceBytes;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SlangCompilerWorkspace::ScopedDirectoryCleanupGuard::~ScopedDirectoryCleanupGuard()noexcept{
    const auto removed = RemoveAllForCleanup(m_path);
    if(removed)
        return;

    const DiagnosticEventText diagnostic = MakeDiagnosticEventText("ShaderCook: failed to remove temporary compiler directory '{}', error {}", m_path.c_str(), removed.error().value());
    CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = __hidden_slang_compiler_workspace::s_DiagnosticWarningEventName, .category = "shader_compiler_cleanup", .message = diagnostic.view(), .file = __FILE__, .line = __LINE__, });
}

Expected<SlangCompilerWorkspace::ScratchPath> SlangCompilerWorkspace::CreateCompilerWorkDirectory(const ScratchPath& parentDirectory, Alloc::ScratchArena& scratchArena){
    auto ensureDirectoriesResult = EnsureDirectories(parentDirectory);
    if(!ensureDirectoriesResult){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to create compiler temporary parent '{}' : {}"), PathToString<tchar>(parentDirectory), StringConvert(ensureDirectoriesResult.error().message()));
        return MakeUnexpected(Failure{});
    }

    for(;;){
        const u64 sequence = __hidden_slang_compiler_workspace::g_TemporarySequence.fetch_add(1u, MemoryOrder::relaxed);
        if(sequence == Limit<u64>::s_Max){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: compiler temporary sequence overflow"));
            return MakeUnexpected(Failure{});
        }
        ScratchString directoryName(".nwb_shader_", scratchArena);
        directoryName.reserve(directoryName.size() + 2u * sizeof(u64) * 2u + 1u);
        AppendHexU64(CurrentProcessId(), directoryName);
        directoryName += '_';
        AppendHexU64(sequence, directoryName);
        ScratchPath directory = parentDirectory / directoryName;
        const auto created = CreateDirectories(directory);
        if(created && *created)
            return directory;
        if(!created){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to create isolated compiler directory '{}' : {}"), PathToString<tchar>(directory), StringConvert(created.error().message()));
            return MakeUnexpected(Failure{});
        }
    }
}

Expected<usize> SlangCompilerWorkspace::CompilerPathTextCapacity(const ScratchPath& path){
    constexpr usize s_MaxEncodedBytesPerUnit = IsSame_V<ScratchPath::value_type, char> ? 1u : 4u;
    if(path.native().size() > Limit<usize>::s_Max / s_MaxEncodedBytesPerUnit){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: compiler path text exceeds representable size"));
        return MakeUnexpected(Failure{});
    }
    return path.native().size() * s_MaxEncodedBytesPerUnit;
}

Expected<SlangCompilerWorkspace::ScratchString> SlangCompilerWorkspace::BuildCompilerPathText(const ScratchPath& path, Alloc::ScratchArena& scratchArena){
    const auto capacity = CompilerPathTextCapacity(path);
    if(!capacity)
        return MakeUnexpected(Failure{});
    ScratchString text{scratchArena};
    text.reserve(*capacity);
    auto out = BackInserter(text);
    BasicStringDetail::WriteConvertedText<char>(out, path.native());
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
    if(!rootText || rootText->size() > (Limit<usize>::s_Max - __hidden_slang_compiler_workspace::s_RootKeyPrefix.size()) / 2u)
        return MakeUnexpected(Failure{});
    ScratchString rootKey(__hidden_slang_compiler_workspace::s_RootKeyPrefix.data(), scratchArena);
    rootKey.reserve(__hidden_slang_compiler_workspace::s_RootKeyPrefix.size() + rootText->size() * 2u);
    static constexpr AStringView s_HexDigits = "0123456789abcdef";
    for(const char ch : *rootText){
        const u8 byte = static_cast<u8>(ch);
        rootKey += s_HexDigits[byte >> s_HexNibbleBits];
        rootKey += s_HexDigits[byte & s_HexNibbleMask];
    }
    return overlayRoot / rootKey / relativePath;
}

bool SlangCompilerWorkspace::IsCompilerDependency(const ShaderCook::CookVector<Path>& dependencies, const ScratchPath& absolutePath)noexcept{
    for(const Path& dependency : dependencies){
        if(dependency == absolutePath)
            return true;
    }
    return false;
}

bool SlangCompilerWorkspace::WriteBomStrippedCompilerInput(ScratchString& source, const Path& sourcePath, const ShaderCook::ShaderCompilerRequest& request, const ScratchPath& overlayRoot, const ScratchPath& outputPath, Alloc::ScratchArena& scratchArena){
    ShaderSourceDependencies::SpliceSourceLines(source);
    ScratchString scanSource(AStringView(source), scratchArena);
    ShaderSourceDependencies::MaskSourceComments(scanSource);
    const AStringView sourceView(source.data(), source.size());
    const AStringView scanView(scanSource);
    usize includeCount = 0u;
    for(usize begin = 0u; begin < scanView.size();){
        const usize foundEnd = scanView.find('\n', begin);
        const usize end = foundEnd == AStringView::npos ? scanView.size() : foundEnd;
        const auto include = ShaderSourceDependencies::ExtractIncludeDirective(scanView.substr(begin, end - begin));
        if(include)
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
        const auto include = ShaderSourceDependencies::ExtractIncludeDirective(scanLine);
        if(include && include->kind == ShaderSourceDependencies::IncludeKind::Unsupported){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: unsupported dependency directive '{}' in '{}'"), StringConvert(TrimView(scanLine)), PathToString<tchar>(sourcePath));
            return false;
        }
        if(include && include->kind != ShaderSourceDependencies::IncludeKind::Macro){
            const ScratchPath includePath(scratchArena, include->name);
            if(includePath.isAbsolute()){
                const auto absoluteIncludePath = AbsolutePath(includePath);
                if(!absoluteIncludePath){
                    NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to resolve compiler include '{}': {}:{}"), PathToString<tchar>(includePath), StringConvert(absoluteIncludePath.error().category().name()), absoluteIncludePath.error().value());
                    return false;
                }
                const ScratchPath normalizedIncludePath = absoluteIncludePath->lexicallyNormal();
                auto overlayIncludePath = BuildCompilerOverlayPath(overlayRoot, normalizedIncludePath, scratchArena);
                if(!IsCompilerDependency(request.dependencies, normalizedIncludePath) || !overlayIncludePath){
                    NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: absolute include '{}' from '{}' is not a mapped compiler dependency"), StringConvert(include->name), PathToString<tchar>(sourcePath));
                    return false;
                }
                const auto pathText = BuildCompilerPathText(*overlayIncludePath, scratchArena);
                if(!pathText)
                    return false;
                if(pathText->size() > Limit<usize>::s_Max - (outputBytes - include->name.size())){
                    NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: BOM-stripped compiler input '{}' exceeds representable size"), PathToString<tchar>(outputPath));
                    return false;
                }
                outputBytes = outputBytes - include->name.size() + pathText->size();
                const usize sourceOffset = begin + static_cast<usize>(include->name.data() - scanLine.data());
                replacements.push_back({ Move(*overlayIncludePath), sourceOffset, include->name.size() });
            }
        }
        begin = end < scanView.size() ? end + 1u : end;
    }
    if(!GlobalFilesystemDetail::CanRepresentStreamSize(static_cast<u64>(outputBytes))){
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
        const auto text = BuildCompilerPathText(replacement.path, scratchArena);
        if(!text)
            return false;
        if(replacement.sourceOffset > sourceCursor)
            stream.write(sourceView.data() + sourceCursor, static_cast<StreamSize>(replacement.sourceOffset - sourceCursor));
        if(!text->empty())
            stream.write(text->data(), static_cast<StreamSize>(text->size()));
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

Expected<SlangCompilerWorkspace::CompilerInputs> SlangCompilerWorkspace::PrepareBomStrippedCompilerInputs(const ShaderCook::ShaderCompilerRequest& request, const ScratchPath& overlayRoot, Alloc::ScratchArena& scratchArena){
    ScratchVector<ScratchPath> includeDirectories(scratchArena);

    if(request.dependencies.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: compiler request for '{}' has no resolved source dependencies"), StringConvert(request.shaderName));
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
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to create temporary compiler source directory '{}' : {}"), PathToString<tchar>(overlayRoot), StringConvert(ensureDirectoriesResult2.error().message()));
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
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to create temporary compiler source parent '{}' : {}"), PathToString<tchar>(overlayDependencyPath->parentPath()), StringConvert(ensureDirectoriesResult3.error().message()));
            return MakeUnexpected(Failure{});
        }

        ScratchString sourceText{scratchArena};
        const auto sourceRead = ReadTextFile(dependency, sourceText);
        if(!sourceRead){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to read compiler input '{}' while writing BOM-stripped source: {}:{}"), PathToString<tchar>(dependency), StringConvert(sourceRead.error().category().name()), sourceRead.error().value());
            return MakeUnexpected(Failure{});
        }
        StripUtf8Bom(sourceText);
        if(!WriteBomStrippedCompilerInput(sourceText, dependency, request, overlayRoot, *overlayDependencyPath, scratchArena))
            return MakeUnexpected(Failure{});
    }

    const auto absoluteSourcePath = AbsolutePath(ScratchPath(scratchArena, request.sourcePath));
    if(!absoluteSourcePath){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to resolve compiler source '{}': {}:{}"), PathToString<tchar>(request.sourcePath), StringConvert(absoluteSourcePath.error().category().name()), absoluteSourcePath.error().value());
        return MakeUnexpected(Failure{});
    }
    const ScratchPath normalizedSourcePath = absoluteSourcePath->lexicallyNormal();
    auto sourcePathResult = BuildCompilerOverlayPath(overlayRoot, normalizedSourcePath, scratchArena);
    if(!IsCompilerDependency(request.dependencies, normalizedSourcePath) || !sourcePathResult){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to map source '{}' into the temporary compiler source directory"), PathToString<tchar>(request.sourcePath));
        return MakeUnexpected(Failure{});
    }

    if(request.includeDirectories.size() > Limit<usize>::s_Max / 2u){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: compiler request for '{}' has too many include directories"), StringConvert(request.shaderName));
        return MakeUnexpected(Failure{});
    }

    includeDirectories.reserve(request.includeDirectories.size() * 2u);
    for(const Path& includeDirectory : request.includeDirectories){
        const auto absoluteIncludeDirectory = AbsolutePath(ScratchPath(scratchArena, includeDirectory));
        if(!absoluteIncludeDirectory){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to resolve compiler include directory '{}': {}:{}"), PathToString<tchar>(includeDirectory), StringConvert(absoluteIncludeDirectory.error().category().name()), absoluteIncludeDirectory.error().value());
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

