// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "slang_compiler.h"
#include "source_dependencies.h"

#include <core/graphics/shader_stage_names.h>
#include <core/graphics/spirv_entry_point.h>
#include <core/common/log.h>

#include <global/filesystem/cleanup.h>
#include <global/hash_utils.h>
#include <global/process_execution.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Alloc = Core::Alloc;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_SLANGC_EXECUTABLE
#error "NWB_SLANGC_EXECUTABLE must be defined by the build configuration"
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_slang_compiler{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using ScratchPath = ::Path<Alloc::ScratchArena>;
using ScratchString = AString<Alloc::ScratchArena>;
template<typename T>
using ScratchVector = Vector<T, Alloc::ScratchArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Slang's SPIR-V 1.5 profile does not include the standard capability set used by the generated Vulkan shaders.
// Declare it explicitly so Slang produces the same output without silently rewriting the requested profile.
static constexpr AStringView s_SpirvBaselineCapabilities[]{ "SPV_KHR_non_semantic_info", "SPV_GOOGLE_user_type", "spvDerivativeControl", "spvImageQuery", "spvImageGatherExtended", "spvSparseResidency", "spvMinLod", "spvFragmentFullyCoveredEXT", };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_CommonCompilerArguments[]{
    "-target", "spirv",
    "-emit-spirv-directly",
    "-fvk-use-entrypoint-name",
    "-warnings-as-errors", "all",
    "-profile", SlangShaderCompiler::s_Spirv15TargetProfileText,
};
static constexpr AStringView s_CapabilityArgument = "-capability";
static constexpr AStringView s_EntryPointArgument = "-entry";
static constexpr AStringView s_StageArgument = "-stage";
static constexpr AStringView s_IncludeArgument = "-I";
static constexpr AStringView s_DefineArgument = "-D";
static constexpr AStringView s_OutputArgument = "-o";
static constexpr usize s_BaseCompilerArgumentCount = 8u + LengthOf(s_CommonCompilerArguments) + LengthOf(s_SpirvBaselineCapabilities) * 2u;
static constexpr usize s_MaxTargetProfileCapabilityCount = 1u;
static constexpr usize s_MaxOptimizationArgumentCount = 1u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ScopedDirectoryCleanupGuard final : NoCopy{
public:
    explicit ScopedDirectoryCleanupGuard(const ScratchPath& path)noexcept
        : m_path(path)
    {}
    ~ScopedDirectoryCleanupGuard()noexcept{
        const auto removed = RemoveAllForCleanup(m_path);
        if(removed)
            return;

        const DiagnosticEventText diagnostic = MakeDiagnosticEventText("ShaderCook: failed to remove temporary compiler directory '{}', error {}", m_path.c_str(), removed.error().value());
        CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = "warning", .category = "shader_compiler_cleanup", .message = diagnostic.view(), .file = __FILE__, .line = __LINE__, });
    }


private:
    const ScratchPath& m_path;
};

Atomic<u64> g_TemporarySequence{0u};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<ScratchPath> CreateCompilerWorkDirectory(const ScratchPath& parentDirectory, Alloc::ScratchArena& scratchArena){
    auto ensureDirectoriesResult = EnsureDirectories(parentDirectory);
    if(!ensureDirectoriesResult){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to create compiler temporary parent '{}' : {}"), PathToString<tchar>(parentDirectory), StringConvert(ensureDirectoriesResult.error().message()));
        return MakeUnexpected(Failure{});
    }

    for(;;){
        const u64 sequence = g_TemporarySequence.fetch_add(1u, MemoryOrder::relaxed);
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<usize> CompilerPathTextCapacity(const ScratchPath& path){
    constexpr usize s_MaxEncodedBytesPerUnit = IsSame_V<ScratchPath::value_type, char> ? 1u : 4u;
    if(path.native().size() > Limit<usize>::s_Max / s_MaxEncodedBytesPerUnit){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: compiler path text exceeds representable size"));
        return MakeUnexpected(Failure{});
    }
    return path.native().size() * s_MaxEncodedBytesPerUnit;
}

[[nodiscard]] static Expected<ScratchString> BuildCompilerPathText(const ScratchPath& path, Alloc::ScratchArena& scratchArena){
    const auto capacity = CompilerPathTextCapacity(path);
    if(!capacity)
        return MakeUnexpected(Failure{});
    ScratchString text{scratchArena};
    text.reserve(*capacity);
    auto out = BackInserter(text);
    BasicStringDetail::WriteConvertedText<char>(out, path.native());
    return text;
}

static Expected<ScratchPath> BuildCompilerOverlayPath(const ScratchPath& overlayRoot, const ScratchPath& absolutePath, Alloc::ScratchArena& scratchArena){
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
    if(!rootText || rootText->size() > (Limit<usize>::s_Max - 5u) / 2u)
        return MakeUnexpected(Failure{});
    ScratchString rootKey("root_", scratchArena);
    rootKey.reserve(5u + rootText->size() * 2u);
    static constexpr AStringView s_HexDigits = "0123456789abcdef";
    for(const char ch : *rootText){
        const u8 byte = static_cast<u8>(ch);
        rootKey += s_HexDigits[byte >> s_HexNibbleBits];
        rootKey += s_HexDigits[byte & s_HexNibbleMask];
    }
    return overlayRoot / rootKey / relativePath;
}


static bool IsCompilerDependency(const ShaderCook::CookVector<Path>& dependencies, const ScratchPath& absolutePath)noexcept{
    for(const Path& dependency : dependencies){
        if(dependency == absolutePath)
            return true;
    }
    return false;
}


struct CompilerIncludeReplacement{
    ScratchPath path;
    usize sourceOffset;
    usize sourceBytes;
};

static bool WriteBomStrippedCompilerInput(ScratchString& source, const Path& sourcePath, const ShaderCook::ShaderCompilerRequest& request, const ScratchPath& overlayRoot, const ScratchPath& outputPath, Alloc::ScratchArena& scratchArena){
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
    ScratchVector<CompilerIncludeReplacement> replacements{scratchArena};
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
    for(const CompilerIncludeReplacement& replacement : replacements){
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

[[nodiscard]] static Expected<ScratchString> BuildCompilerDefineArgument(const ShaderCook::ShaderMacroDefinition& define, const ShaderCook::ShaderCompilerRequest& request, const ScratchPath& overlayRoot, Alloc::ScratchArena& scratchArena){
    const bool plannedMacroInclude = request.compilerInputsHaveBom && FindIf(request.externallyPlannedMacroIncludes.begin(), request.externallyPlannedMacroIncludes.end(), [&define](const AStringView name)noexcept{
            return name == define.name;
        }) != request.externallyPlannedMacroIncludes.end();
    const AStringView includeName = plannedMacroInclude ? UnquoteDoubleQuotedView(define.value) : AStringView();
    const ScratchPath includePath(scratchArena, includeName);
    Expected<ScratchPath> overlayIncludePath = MakeUnexpected(Failure{});
    usize valueCapacity = define.value.size();
    if(!includeName.empty() && includePath.isAbsolute()){
        const auto absoluteIncludePath = AbsolutePath(includePath);
        if(!absoluteIncludePath){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to resolve planned macro include '{}': {}:{}"), PathToString<tchar>(includePath), StringConvert(absoluteIncludePath.error().category().name()), absoluteIncludePath.error().value());
            return MakeUnexpected(Failure{});
        }
        const ScratchPath normalizedIncludePath = absoluteIncludePath->lexicallyNormal();
        overlayIncludePath = BuildCompilerOverlayPath(overlayRoot, normalizedIncludePath, scratchArena);
        if(!IsCompilerDependency(request.dependencies, normalizedIncludePath) || !overlayIncludePath){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: planned macro include '{}' is not a mapped compiler dependency"), StringConvert(includeName));
            return MakeUnexpected(Failure{});
        }
        const auto capacity = CompilerPathTextCapacity(*overlayIncludePath);
        if(!capacity || *capacity > Limit<usize>::s_Max - 2u)
            return MakeUnexpected(Failure{});
        valueCapacity = *capacity + 2u;
    }
    const usize fixedBytes = s_DefineArgument.size() + (!define.value.empty() ? 1u : 0u);
    if(define.name.size() > Limit<usize>::s_Max - fixedBytes || valueCapacity > Limit<usize>::s_Max - fixedBytes - define.name.size())
        return MakeUnexpected(Failure{});
    ScratchString argument{scratchArena};
    argument.reserve(fixedBytes + define.name.size() + valueCapacity);
    argument += s_DefineArgument;
    argument += define.name;
    if(!define.value.empty()){
        argument += '=';
        if(overlayIncludePath){
            argument += '"';
            auto out = BackInserter(argument);
            BasicStringDetail::WriteConvertedText<char>(out, overlayIncludePath->native());
            argument += '"';
        }
        else
            argument += define.value;
    }
    return argument;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<ScratchPath> PrepareBomStrippedCompilerInputs(const ShaderCook::ShaderCompilerRequest& request, const ScratchPath& overlayRoot, ScratchVector<ScratchPath>& outIncludeDirectories, Alloc::ScratchArena& scratchArena){
    outIncludeDirectories.clear();

    if(request.dependencies.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: compiler request for '{}' has no resolved source dependencies"), StringConvert(request.shaderName));
        return MakeUnexpected(Failure{});
    }

    if(!request.compilerInputsHaveBom){
        outIncludeDirectories.reserve(request.includeDirectories.size());
        for(const Path& includeDirectory : request.includeDirectories)
            outIncludeDirectories.emplace_back(scratchArena, includeDirectory);
        return ScratchPath(scratchArena, request.sourcePath);
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

    outIncludeDirectories.reserve(request.includeDirectories.size() * 2u);
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
        outIncludeDirectories.push_back(Move(*overlayIncludeDirectory));
    }
    for(const Path& includeDirectory : request.includeDirectories)
        outIncludeDirectories.emplace_back(scratchArena, includeDirectory);

    return Move(*sourcePathResult);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SlangStageMapping{
    AStringView stage;
    AStringView slangStage;
};

static constexpr SlangStageMapping s_StageMappings[]{
    { Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::VertexStage), "vertex" },
    { Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::HullStage), "hull" },
    { Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::DomainStage), "domain" },
    { Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::GeometryStage), "geometry" },
    { Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::PixelStage), "fragment" },
    { Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::ComputeStage), "compute" },
    { Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::AmplificationStage), "amplification" },
    { Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::MeshStage), "mesh" },
    { Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::RayGenerationStage), "raygeneration" },
    { Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::AnyHitStage), "anyhit" },
    { Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::ClosestHitStage), "closesthit" },
    { Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::MissStage), "miss" },
    { Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::IntersectionStage), "intersection" },
    { Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::CallableStage), "callable" },
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<AStringView> TryMapStageToSlangStage(const AStringView stage)noexcept{
    for(const SlangStageMapping& mapping : s_StageMappings){
        if(stage == mapping.stage){
            return mapping.slangStage;
        }
    }

    return MakeUnexpected(Failure{});
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static AStringView SlangOptimizationArgument(const ShaderOptimizationLevel::Enum optimizationLevel)noexcept{
    switch(optimizationLevel){
    case ShaderOptimizationLevel::None: return "-O0";
    case ShaderOptimizationLevel::Default: return {};
    case ShaderOptimizationLevel::High: return "-O2";
    case ShaderOptimizationLevel::Maximal: return "-O3";
    default: return {};
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<u64> SlangShaderCompiler::ComputeCompilerFingerprint(const Path& temporaryRoot, Alloc::ScratchArena& scratchArena){
    u64 fingerprint = s_Fnv64OffsetBasis;
    const __hidden_slang_compiler::ScratchPath compilerPath(scratchArena, NWB_SLANGC_EXECUTABLE);
    InputFileStream compilerStream(compilerPath, s_FileOpenBinary);
    if(!compilerStream.is_open()){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to open configured compiler '{}' for fingerprinting"), PathToString<tchar>(compilerPath));
        return MakeUnexpected(Failure{});
    }

    Array<char, 8192u> compilerBytes{};
    for(;;){
        compilerStream.read(compilerBytes.data(), static_cast<StreamSize>(compilerBytes.size()));
        const StreamSize bytesRead = compilerStream.gcount();
        if(bytesRead > 0)
            fingerprint = UpdateFnv64(fingerprint, reinterpret_cast<const u8*>(compilerBytes.data()), static_cast<usize>(bytesRead));
        if(compilerStream.eof() && !compilerStream.bad())
            break;
        if(!compilerStream.good()){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to read configured compiler '{}' for fingerprinting"), PathToString<tchar>(compilerPath));
            return MakeUnexpected(Failure{});
        }
    }

    const auto workDirectoryResult = __hidden_slang_compiler::CreateCompilerWorkDirectory(__hidden_slang_compiler::ScratchPath(scratchArena, temporaryRoot), scratchArena);
    if(!workDirectoryResult)
        return MakeUnexpected(Failure{});
    const __hidden_slang_compiler::ScratchPath& workDirectory = *workDirectoryResult;
    __hidden_slang_compiler::ScopedDirectoryCleanupGuard workDirectoryCleanup(workDirectory);
    const __hidden_slang_compiler::ScratchPath versionPath = workDirectory / "compiler.version";
    const auto versionPathText = __hidden_slang_compiler::BuildCompilerPathText(versionPath, scratchArena);
    if(!versionPathText)
        return MakeUnexpected(Failure{});
    const AStringView arguments[]{ AStringView(NWB_SLANGC_EXECUTABLE), "-version" };
    const auto exitCode = RunProcessRedirectedToFile(scratchArena, arguments, AStringView(*versionPathText));
    if(!exitCode){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to execute configured compiler '{}' for version query: process execution failure {}"), PathToString<tchar>(compilerPath), static_cast<u32>(exitCode.error()));
        return MakeUnexpected(Failure{});
    }
    if(*exitCode != 0){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: configured compiler '{}' version query failed with exit code {}"), PathToString<tchar>(compilerPath), *exitCode);
        return MakeUnexpected(Failure{});
    }
    __hidden_slang_compiler::ScratchString versionText(scratchArena);
    const auto versionRead = ReadBinaryFile(versionPath, versionText);
    if(!versionRead){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to read configured compiler version '{}': {}:{}"), PathToString<tchar>(versionPath), StringConvert(versionRead.error().category().name()), versionRead.error().value());
        return MakeUnexpected(Failure{});
    }
    if(TrimView(versionText).empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: configured compiler '{}' returned empty version text"), PathToString<tchar>(compilerPath));
        return MakeUnexpected(Failure{});
    }

    const auto appendText = [&fingerprint](const AStringView text)noexcept{
        Fnv64AppendBuffer(fingerprint, reinterpret_cast<const u8*>(text.data()), text.size());
    };
    // The version identifies the loaded Slang compiler library as well as its executable launcher.
    appendText(TrimView(versionText));
    for(const AStringView argument : __hidden_slang_compiler::s_CommonCompilerArguments)
        appendText(argument);
    for(const AStringView capability : __hidden_slang_compiler::s_SpirvBaselineCapabilities){
        appendText(__hidden_slang_compiler::s_CapabilityArgument);
        appendText(capability);
    }
    appendText(SlangShaderCompiler::s_SpvRayQueryCapabilityText);
    appendText(__hidden_slang_compiler::s_EntryPointArgument);
    appendText(__hidden_slang_compiler::s_StageArgument);
    appendText(__hidden_slang_compiler::s_IncludeArgument);
    appendText(__hidden_slang_compiler::s_DefineArgument);
    appendText(__hidden_slang_compiler::s_OutputArgument);
    for(const __hidden_slang_compiler::SlangStageMapping& mapping : __hidden_slang_compiler::s_StageMappings){
        appendText(mapping.stage);
        appendText(mapping.slangStage);
    }
    for(u8 optimizationLevel = 0u; optimizationLevel < ShaderOptimizationLevel::kCount; ++optimizationLevel)
        appendText(__hidden_slang_compiler::SlangOptimizationArgument(static_cast<ShaderOptimizationLevel::Enum>(optimizationLevel)));
    return fingerprint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool SlangShaderCompiler::CompileVariant(const ShaderCook::ShaderCompilerRequest& request, ShaderCook::CookVector<u8>& outBytecode, Core::Alloc::ScratchArena& scratchArena){
    outBytecode.clear();

    if(request.sourcePath.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to compile shader '{}' : source path is empty"), StringConvert(request.shaderName));
        return false;
    }

    if(request.outputPath.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to compile shader '{}' : output path is empty"), StringConvert(request.shaderName));
        return false;
    }

    if(request.entryPoint.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to compile shader '{}' : entry point is empty"), StringConvert(request.shaderName));
        return false;
    }

    const auto slangStage = __hidden_slang_compiler::TryMapStageToSlangStage(request.stage);
    if(!slangStage){
        NWB_LOGGER_ERROR(NWB_TEXT("Unknown shader stage '{}' in entry '{}'"), StringConvert(request.stage), StringConvert(request.shaderName));
        return false;
    }

    const AStringView optimizationArgument = __hidden_slang_compiler::SlangOptimizationArgument(request.optimizationLevel);
    if(request.optimizationLevel >= ShaderOptimizationLevel::kCount){
        NWB_LOGGER_ERROR(NWB_TEXT("Shader '{}' uses an invalid optimization level {}"), StringConvert(request.shaderName), static_cast<u32>(request.optimizationLevel));
        return false;
    }

    const __hidden_slang_compiler::ScratchPath outputParent = request.outputPath.parentPath();
    const auto compilerWorkDirectoryResult = __hidden_slang_compiler::CreateCompilerWorkDirectory(outputParent.empty() ? __hidden_slang_compiler::ScratchPath(scratchArena, ".") : outputParent, scratchArena);
    if(!compilerWorkDirectoryResult)
        return false;
    const __hidden_slang_compiler::ScratchPath& compilerWorkDirectory = *compilerWorkDirectoryResult;
    __hidden_slang_compiler::ScopedDirectoryCleanupGuard workDirectoryCleanup(compilerWorkDirectory);
    const __hidden_slang_compiler::ScratchPath diagnosticsPath = compilerWorkDirectory / "compiler.diag";
    const __hidden_slang_compiler::ScratchPath compilerOutputPath = compilerWorkDirectory / "module.spv";
    const __hidden_slang_compiler::ScratchPath compilerOverlayRoot = compilerWorkDirectory / "sources";
    __hidden_slang_compiler::ScratchVector<__hidden_slang_compiler::ScratchPath> compilerIncludeDirectories(scratchArena);
    const auto compilerSourcePath = __hidden_slang_compiler::PrepareBomStrippedCompilerInputs(request, compilerOverlayRoot, compilerIncludeDirectories, scratchArena);
    if(!compilerSourcePath)
        return false;

    const usize includeDirectoryCount = compilerIncludeDirectories.size();
    const usize defineCount = static_cast<usize>(request.defineCount);
    constexpr usize s_MaxFixedArgumentCount = __hidden_slang_compiler::s_BaseCompilerArgumentCount + __hidden_slang_compiler::s_MaxTargetProfileCapabilityCount * 2u + __hidden_slang_compiler::s_MaxOptimizationArgumentCount;
    if(AddOverflows<usize>(s_MaxFixedArgumentCount, defineCount) || includeDirectoryCount > (Limit<usize>::s_Max - s_MaxFixedArgumentCount - defineCount) / 2u){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: compiler request for '{}' has too many arguments"), StringConvert(request.shaderName));
        return false;
    }

    const usize argumentReserve = s_MaxFixedArgumentCount + includeDirectoryCount * 2u + defineCount;
    const usize ownedArgumentCount = 2u + includeDirectoryCount + defineCount;
    __hidden_slang_compiler::ScratchVector<AStringView> arguments(scratchArena);
    arguments.reserve(argumentReserve);
    __hidden_slang_compiler::ScratchVector<__hidden_slang_compiler::ScratchString> ownedArguments(scratchArena);
    // Retire nested string allocations before the owner array, independently of STL destruction order.
    ScopeExit retireArguments([&ownedArguments]()noexcept{
        while(!ownedArguments.empty())
            ownedArguments.pop_back();
    });
    // Reserve owners before publishing views so inline string storage cannot move.
    ownedArguments.reserve(ownedArgumentCount);
    arguments.push_back(AStringView(NWB_SLANGC_EXECUTABLE));

    auto sourcePathText = __hidden_slang_compiler::BuildCompilerPathText(*compilerSourcePath, scratchArena);
    if(!sourcePathText)
        return false;
    ownedArguments.push_back(Move(*sourcePathText));
    arguments.push_back(AStringView(ownedArguments.back()));
    for(const AStringView argument : __hidden_slang_compiler::s_CommonCompilerArguments)
        arguments.push_back(argument);
    if(!optimizationArgument.empty())
        arguments.push_back(optimizationArgument);
    if(request.rayQuery){
        arguments.push_back(__hidden_slang_compiler::s_CapabilityArgument);
        arguments.push_back(SlangShaderCompiler::s_SpvRayQueryCapabilityText);
    }
    for(const AStringView capability : __hidden_slang_compiler::s_SpirvBaselineCapabilities){
        arguments.push_back(__hidden_slang_compiler::s_CapabilityArgument);
        arguments.push_back(capability);
    }
    arguments.push_back(__hidden_slang_compiler::s_EntryPointArgument);
    arguments.push_back(request.entryPoint);
    arguments.push_back(__hidden_slang_compiler::s_StageArgument);
    arguments.push_back(*slangStage);
    for(const __hidden_slang_compiler::ScratchPath& includeDirectory : compilerIncludeDirectories){
        arguments.push_back(__hidden_slang_compiler::s_IncludeArgument);
        auto includeDirectoryText = __hidden_slang_compiler::BuildCompilerPathText(includeDirectory, scratchArena);
        if(!includeDirectoryText)
            return false;
        ownedArguments.push_back(Move(*includeDirectoryText));
        arguments.push_back(AStringView(ownedArguments.back()));
    }

    for(u32 i = 0u; i < request.defineCount; ++i){
        auto defineArgument = __hidden_slang_compiler::BuildCompilerDefineArgument(request.defines[i], request, compilerOverlayRoot, scratchArena);
        if(!defineArgument)
            return false;
        ownedArguments.push_back(Move(*defineArgument));
        arguments.push_back(AStringView(ownedArguments.back()));
    }

    arguments.push_back(__hidden_slang_compiler::s_OutputArgument);
    auto outputPathText = __hidden_slang_compiler::BuildCompilerPathText(compilerOutputPath, scratchArena);
    if(!outputPathText)
        return false;
    ownedArguments.push_back(Move(*outputPathText));
    arguments.push_back(AStringView(ownedArguments.back()));

    const auto diagnosticsPathText = __hidden_slang_compiler::BuildCompilerPathText(diagnosticsPath, scratchArena);
    if(!diagnosticsPathText)
        return false;
    const auto exitCode = ::RunProcessRedirectedToFile(scratchArena, arguments, AStringView(*diagnosticsPathText));
    if(!exitCode || *exitCode != 0){
        const TStringView failureStage = exitCode ? NWB_TEXT("compiler exit code") : NWB_TEXT("process execution failure");
        const i64 failureCode = exitCode ? static_cast<i64>(*exitCode) : static_cast<i64>(exitCode.error());
        __hidden_slang_compiler::ScratchString diagnostics{scratchArena};
        const auto diagnosticsRead = ReadBinaryFile(diagnosticsPath, diagnostics);
        if(!diagnosticsRead){
            NWB_LOGGER_ERROR(NWB_TEXT("Shader compile failed for '{}' (variant '{}'): {} {}; diagnostics read '{}' failed: {}:{}")
                , StringConvert(request.shaderName)
                , StringConvert(request.variantName)
                , failureStage
                , failureCode
                , PathToString<tchar>(diagnosticsPath)
                , StringConvert(diagnosticsRead.error().category().name())
                , diagnosticsRead.error().value()
            );
        }
        else if(!diagnostics.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Shader compile failed for '{}' (variant '{}'): {} {} :\n{}"), StringConvert(request.shaderName), StringConvert(request.variantName), failureStage, failureCode, StringConvert(diagnostics));
        }
        else{
            NWB_LOGGER_ERROR(NWB_TEXT("Shader compile failed for '{}' (variant '{}'): {} {}"), StringConvert(request.shaderName), StringConvert(request.variantName), failureStage, failureCode);
        }
        return false;
    }

    __hidden_slang_compiler::ScratchString diagnostics{scratchArena};
    const auto diagnosticsRead = ReadBinaryFile(diagnosticsPath, diagnostics);
    if(!diagnosticsRead){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to read compiler diagnostics '{}' for '{}' (variant '{}'): {}:{}")
            , PathToString<tchar>(diagnosticsPath)
            , StringConvert(request.shaderName)
            , StringConvert(request.variantName)
            , StringConvert(diagnosticsRead.error().category().name())
            , diagnosticsRead.error().value()
        );
        return false;
    }
    if(!diagnostics.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Shader compiler emitted unexpected diagnostics for '{}' (variant '{}') :\n{}"), StringConvert(request.shaderName), StringConvert(request.variantName), StringConvert(diagnostics));
        return false;
    }

    auto readBinaryFileResult = ReadBinaryFile(compilerOutputPath, outBytecode);
    if(!readBinaryFileResult){
        if(readBinaryFileResult.error()){
            NWB_LOGGER_ERROR(NWB_TEXT("Shader compile failed for '{}' (variant '{}') : failed to read output '{}' : {}")
                , StringConvert(request.shaderName)
                , StringConvert(request.variantName)
                , PathToString<tchar>(compilerOutputPath)
                , StringConvert(readBinaryFileResult.error().message())
            );
        }
        else{
            NWB_LOGGER_ERROR(NWB_TEXT("Shader compile failed for '{}' (variant '{}') : failed to read output '{}'"), StringConvert(request.shaderName), StringConvert(request.variantName), PathToString<tchar>(compilerOutputPath));
        }
        return false;
    }

    const auto stageName = Name::FromText(request.stage);
    if(!stageName){
        NWB_LOGGER_ERROR(NWB_TEXT("Shader compile failed for '{}' (variant '{}'): shader stage contains an embedded null"), StringConvert(request.shaderName), StringConvert(request.variantName));
        outBytecode.clear();
        return false;
    }
    const Core::ShaderType::Enum shaderType = Core::ShaderStageNames::ShaderTypeFromArchiveStageName(*stageName);
    if(!Core::ResolveSpirvEntryPointName(BinaryByteView{ outBytecode.data(), outBytecode.size() }, request.entryPoint, Core::ShaderType::ToMask(shaderType))){
        NWB_LOGGER_ERROR(NWB_TEXT("Shader compile failed for '{}' (variant '{}'): malformed SPIR-V or wrong physical stage/entry point"), StringConvert(request.shaderName), StringConvert(request.variantName));
        outBytecode.clear();
        return false;
    }

    auto renamePathResult = RenamePath(compilerOutputPath, request.outputPath);
    if(!renamePathResult){
        __hidden_slang_compiler::ScratchVector<u8> publishedBytecode(scratchArena);
        if(
            ReadBinaryFile(request.outputPath, publishedBytecode)
            && publishedBytecode.size() == outBytecode.size()
            && NWB_MEMCMP(publishedBytecode.data(), outBytecode.data(), outBytecode.size()) == 0
            && Core::ResolveSpirvEntryPointName(BinaryByteView{ publishedBytecode.data(), publishedBytecode.size() }, request.entryPoint, Core::ShaderType::ToMask(shaderType))
        )
            return true;
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to publish verified bytecode cache '{}' : {}"), PathToString<tchar>(request.outputPath), StringConvert(renamePathResult.error().message()));
        outBytecode.clear();
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

