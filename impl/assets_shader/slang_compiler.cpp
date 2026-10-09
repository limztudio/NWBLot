// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "slang_compiler.h"
#include "source_dependencies.h"
#include "arena_names.h"

#include <core/graphics/shader_stage_names.h>
#include <core/graphics/spirv_entry_point.h>
#include <core/common/log.h>
#include <global/hash_utils.h>
#include <global/process_execution.h>


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


using ScratchString = AString<Alloc::ScratchArena>;
template<typename T>
using ScratchVector = Vector<T, Alloc::ScratchArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Slang's SPIR-V 1.5 profile does not include the standard capability set used by the generated Vulkan shaders.
// Declare it explicitly so Slang produces the same output without silently rewriting the requested profile.
static constexpr AStringView s_SpirvBaselineCapabilities[]{
    "SPV_KHR_non_semantic_info",
    "SPV_GOOGLE_user_type",
    "spvDerivativeControl",
    "spvImageQuery",
    "spvImageGatherExtended",
    "spvSparseResidency",
    "spvMinLod",
    "spvFragmentFullyCoveredEXT",
};


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


template<typename StringT>
static Expected<void, ErrorCode> ReadDiagnostics(const Path& diagnosticsPath, StringT& outDiagnostics){
    return ReadBinaryFile(diagnosticsPath, outDiagnostics);
}

class ScopedDirectoryCleanupGuard final : NoCopy{
public:
    explicit ScopedDirectoryCleanupGuard(const Path& path)noexcept
        : m_path(path)
    {}
    ~ScopedDirectoryCleanupGuard(){
        const auto removed = RemoveAllIfExists(m_path);
        if(removed)
            return;

        NWB_LOGGER_WARNING(NWB_TEXT("ShaderCook: failed to remove temporary compiler directory '{}' : {}")
            , PathToString<tchar>(m_path)
            , StringConvert(removed.error().message())
        );
    }


private:
    const Path& m_path;
};

Atomic<u64> g_TemporarySequence{0u};

static Expected<Path> CreateCompilerWorkDirectory(const Path& parentDirectory){
    auto ensureDirectoriesResult = EnsureDirectories(parentDirectory);
    if(!ensureDirectoriesResult){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to create compiler temporary parent '{}' : {}")
            , PathToString<tchar>(parentDirectory)
            , StringConvert(ensureDirectoriesResult.error().message())
        );
        return MakeUnexpected(Failure{});
    }

    for(;;){
        const u64 sequence = g_TemporarySequence.fetch_add(1u, MemoryOrder::relaxed);
        if(sequence == Limit<u64>::s_Max){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: compiler temporary sequence overflow"));
            return MakeUnexpected(Failure{});
        }
        ShaderCook::CookString directoryName(".nwb_shader_", parentDirectory.arena());
        AppendHexU64(CurrentProcessId(), directoryName);
        directoryName += '_';
        AppendHexU64(sequence, directoryName);
        Path directory = parentDirectory / directoryName;
        const auto created = CreateDirectories(directory);
        if(created && *created)
            return directory;
        if(!created){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to create isolated compiler directory '{}' : {}")
                , PathToString<tchar>(directory)
                , StringConvert(created.error().message())
            );
            return MakeUnexpected(Failure{});
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<Path> BuildCompilerOverlayPath(const Path& overlayRoot, const Path& absolutePath){
    if(!absolutePath.isAbsolute())
        return MakeUnexpected(Failure{});

    const auto rootIt = absolutePath.begin();
    if(rootIt == absolutePath.end())
        return MakeUnexpected(Failure{});

    const Path rootPath(absolutePath.arena(), (*rootIt).native());
    const Path relativePath = absolutePath.lexicallyRelative(rootPath);
    if(relativePath.empty())
        return MakeUnexpected(Failure{});

    const ShaderCook::CookString rootText = PathToString<char>(overlayRoot.arena(), rootPath);
    if(rootText.size() > (Limit<usize>::s_Max - 5u) / 2u)
        return MakeUnexpected(Failure{});
    ShaderCook::CookString rootKey("root_", overlayRoot.arena());
    rootKey.reserve(5u + rootText.size() * 2u);
    static constexpr AStringView s_HexDigits = "0123456789abcdef";
    for(const char ch : rootText){
        const u8 byte = static_cast<u8>(ch);
        rootKey += s_HexDigits[byte >> s_HexNibbleBits];
        rootKey += s_HexDigits[byte & s_HexNibbleMask];
    }
    return overlayRoot / rootKey / relativePath;
}


static bool IsCompilerDependency(const ShaderCook::CookVector<Path>& dependencies, const Path& absolutePath){
    for(const Path& dependency : dependencies){
        if(dependency == absolutePath)
            return true;
    }
    return false;
}


static bool RewriteAbsoluteCompilerIncludes(
    ScratchString& inOutSource,
    const Path& sourcePath,
    const ShaderCook::ShaderCompilerRequest& request,
    const Path& overlayRoot,
    Alloc::ScratchArena& scratchArena
){
    ShaderSourceDependencies::SpliceSourceLines(inOutSource);
    ScratchString scanSource(AStringView(inOutSource), scratchArena);
    ShaderSourceDependencies::MaskSourceComments(scanSource);
    ScratchString rewrittenSource{scratchArena};
    rewrittenSource.reserve(inOutSource.size());

    const AStringView sourceView(inOutSource.data(), inOutSource.size());
    const AStringView scanView(scanSource);
    usize lineBegin = 0u;
    while(lineBegin < sourceView.size()){
        usize lineEnd = lineBegin;
        while(lineEnd < scanView.size() && scanView[lineEnd] != '\n')
            ++lineEnd;

        const AStringView line = sourceView.substr(lineBegin, lineEnd - lineBegin);
        const AStringView scanLine = scanView.substr(lineBegin, lineEnd - lineBegin);
        const auto include = ShaderSourceDependencies::ExtractIncludeDirective(scanLine);
        if(!include || include->kind == ShaderSourceDependencies::IncludeKind::Macro){
            rewrittenSource.append(line.data(), line.size());
        }
        else{
            const AStringView includeName = include->name;
            const auto includeKind = include->kind;
            if(includeKind == ShaderSourceDependencies::IncludeKind::Unsupported){
                NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: unsupported dependency directive '{}' in '{}'"), StringConvert(TrimView(scanLine)), PathToString<tchar>(sourcePath));
                return false;
            }
            Path includePath(sourcePath.arena(), includeName);
            if(!includePath.isAbsolute()){
                rewrittenSource.append(line.data(), line.size());
            }
            else{
                auto absoluteIncludePath = AbsolutePath(includePath);
                if(absoluteIncludePath)
                    *absoluteIncludePath = absoluteIncludePath->lexicallyNormal();
                const auto overlayIncludePath = absoluteIncludePath ? BuildCompilerOverlayPath(overlayRoot, *absoluteIncludePath) : Expected<Path>(MakeUnexpected(Failure{}));
                if(
                    !absoluteIncludePath
                    || !IsCompilerDependency(request.dependencies, *absoluteIncludePath)
                    || !overlayIncludePath
                ){
                    NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: absolute include '{}' from '{}' is not a mapped compiler dependency")
                        , StringConvert(includeName)
                        , PathToString<tchar>(sourcePath)
                    );
                    return false;
                }
                const usize includeNameOffset = static_cast<usize>(includeName.data() - scanLine.data());
                const ScratchString overlayIncludeText = PathToString<char>(scratchArena, *overlayIncludePath);
                rewrittenSource.append(line.data(), includeNameOffset);
                rewrittenSource += overlayIncludeText;
                rewrittenSource.append(
                    line.data() + includeNameOffset + includeName.size(),
                    line.size() - includeNameOffset - includeName.size()
                );
            }
        }

        if(lineEnd < sourceView.size())
            rewrittenSource += '\n';
        lineBegin = lineEnd + 1u;
    }

    inOutSource = Move(rewrittenSource);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<Path> PrepareBomStrippedCompilerInputs(
    const ShaderCook::ShaderCompilerRequest& request,
    const Path& overlayRoot,
    ScratchVector<Path>& outIncludeDirectories,
    Alloc::ScratchArena& scratchArena
){
    Path sourcePath = request.sourcePath;
    outIncludeDirectories.clear();

    if(request.dependencies.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: compiler request for '{}' has no resolved source dependencies"), StringConvert(request.shaderName));
        return MakeUnexpected(Failure{});
    }

    if(!request.compilerInputsHaveBom){
        outIncludeDirectories.reserve(request.includeDirectories.size());
        for(const Path& includeDirectory : request.includeDirectories)
            outIncludeDirectories.push_back(includeDirectory);
        return sourcePath;
    }

    auto ensureDirectoriesResult2 = EnsureDirectories(overlayRoot);
    if(!ensureDirectoriesResult2){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to create temporary compiler source directory '{}' : {}")
            , PathToString<tchar>(overlayRoot)
            , StringConvert(ensureDirectoriesResult2.error().message())
        );
        return MakeUnexpected(Failure{});
    }

    ScratchString sourceText{scratchArena};
    // Encode each absolute root separately and preserve nested relative includes within that root.
    for(const Path& dependency : request.dependencies){
        const auto overlayDependencyPath = BuildCompilerOverlayPath(overlayRoot, dependency);
        if(!overlayDependencyPath){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to map compiler input '{}' into the temporary source directory")
                , PathToString<tchar>(dependency)
            );
            return MakeUnexpected(Failure{});
        }

        auto ensureDirectoriesResult3 = EnsureDirectories(overlayDependencyPath->parentPath());
        if(!ensureDirectoriesResult3){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to create temporary compiler source parent '{}' : {}")
                , PathToString<tchar>(overlayDependencyPath->parentPath())
                , StringConvert(ensureDirectoriesResult3.error().message())
            );
            return MakeUnexpected(Failure{});
        }

        sourceText.clear();
        if(!ReadTextFile(dependency, sourceText)){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to read compiler input '{}' while writing BOM-stripped source")
                , PathToString<tchar>(dependency)
            );
            return MakeUnexpected(Failure{});
        }
        StripUtf8Bom(sourceText);
        if(!RewriteAbsoluteCompilerIncludes(sourceText, dependency, request, overlayRoot, scratchArena))
            return MakeUnexpected(Failure{});
        if(!WriteTextFile(*overlayDependencyPath, AStringView(sourceText.data(), sourceText.size()))){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to write BOM-stripped compiler input '{}'"), PathToString<tchar>(*overlayDependencyPath));
            return MakeUnexpected(Failure{});
        }
    }

    auto absoluteSourcePath = AbsolutePath(request.sourcePath);
    if(absoluteSourcePath)
        *absoluteSourcePath = absoluteSourcePath->lexicallyNormal();
    const auto sourcePathResult = absoluteSourcePath ? BuildCompilerOverlayPath(overlayRoot, *absoluteSourcePath) : Expected<Path>(MakeUnexpected(Failure{}));
    if(!absoluteSourcePath || !IsCompilerDependency(request.dependencies, *absoluteSourcePath) || !sourcePathResult){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to map source '{}' into the temporary compiler source directory")
            , PathToString<tchar>(request.sourcePath)
        );
        return MakeUnexpected(Failure{});
    }

    sourcePath = *sourcePathResult;
    if(request.includeDirectories.size() > Limit<usize>::s_Max / 2u){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: compiler request for '{}' has too many include directories"), StringConvert(request.shaderName));
        return MakeUnexpected(Failure{});
    }

    outIncludeDirectories.reserve(request.includeDirectories.size() * 2u);
    for(const Path& includeDirectory : request.includeDirectories){
        auto absoluteIncludeDirectory = AbsolutePath(includeDirectory);
        if(absoluteIncludeDirectory)
            *absoluteIncludeDirectory = absoluteIncludeDirectory->lexicallyNormal();
        auto overlayIncludeDirectory = absoluteIncludeDirectory ? BuildCompilerOverlayPath(overlayRoot, *absoluteIncludeDirectory) : Expected<Path>(MakeUnexpected(Failure{}));
        if(!overlayIncludeDirectory){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to map include directory '{}' into the temporary compiler source directory")
                , PathToString<tchar>(includeDirectory)
            );
            return MakeUnexpected(Failure{});
        }
        outIncludeDirectories.push_back(Move(*overlayIncludeDirectory));
    }
    for(const Path& includeDirectory : request.includeDirectories)
        outIncludeDirectories.push_back(includeDirectory);

    return sourcePath;
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


Expected<u64> SlangShaderCompiler::ComputeCompilerFingerprint(
    const Path& temporaryRoot,
    Alloc::ScratchArena& scratchArena
){
    u64 fingerprint = s_Fnv64OffsetBasis;
    const Path compilerPath(temporaryRoot.arena(), NWB_SLANGC_EXECUTABLE);
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

    const auto workDirectoryResult = __hidden_slang_compiler::CreateCompilerWorkDirectory(temporaryRoot);
    if(!workDirectoryResult)
        return MakeUnexpected(Failure{});
    const Path& workDirectory = *workDirectoryResult;
    __hidden_slang_compiler::ScopedDirectoryCleanupGuard workDirectoryCleanup(workDirectory);
    const Path versionPath = workDirectory / "compiler.version";
    const __hidden_slang_compiler::ScratchString versionPathText = PathToString<char>(scratchArena, versionPath);
    const AStringView arguments[]{ AStringView(NWB_SLANGC_EXECUTABLE), "-version" };
    const auto exitCode = RunProcessRedirectedToFile(scratchArena, arguments, AStringView(versionPathText));
    __hidden_slang_compiler::ScratchString versionText(scratchArena);
    if(!exitCode || *exitCode != 0 || !__hidden_slang_compiler::ReadDiagnostics(versionPath, versionText) || TrimView(versionText).empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to query configured compiler version '{}'"), PathToString<tchar>(compilerPath));
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


bool SlangShaderCompiler::CompileVariant(const ShaderCook::ShaderCompilerRequest& request, ShaderCook::CookVector<u8>& outBytecode){
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
        NWB_LOGGER_ERROR(NWB_TEXT("Shader '{}' uses an invalid optimization level {}")
            , StringConvert(request.shaderName)
            , static_cast<u32>(request.optimizationLevel)
        );
        return false;
    }

    Alloc::ScratchArena argumentArena(AssetsShaderArenaScope::s_CompilerArgumentsArena);
    const Path outputParent = request.outputPath.parentPath();
    const auto compilerWorkDirectoryResult = __hidden_slang_compiler::CreateCompilerWorkDirectory(outputParent.empty() ? Path(outputParent.arena(), ".") : outputParent);
    if(!compilerWorkDirectoryResult)
        return false;
    const Path& compilerWorkDirectory = *compilerWorkDirectoryResult;
    __hidden_slang_compiler::ScopedDirectoryCleanupGuard workDirectoryCleanup(compilerWorkDirectory);
    const Path diagnosticsPath = compilerWorkDirectory / "compiler.diag";
    const Path compilerOutputPath = compilerWorkDirectory / "module.spv";
    const Path compilerOverlayRoot = compilerWorkDirectory / "sources";
    __hidden_slang_compiler::ScratchVector<Path> compilerIncludeDirectories(argumentArena);
    const auto compilerSourcePath = __hidden_slang_compiler::PrepareBomStrippedCompilerInputs(
        request,
        compilerOverlayRoot,
        compilerIncludeDirectories,
        argumentArena
    );
    if(!compilerSourcePath)
        return false;

    const usize includeDirectoryCount = compilerIncludeDirectories.size();
    const usize defineCount = static_cast<usize>(request.defineCount);
    constexpr usize maxFixedArgumentCount = __hidden_slang_compiler::s_BaseCompilerArgumentCount
        + __hidden_slang_compiler::s_MaxTargetProfileCapabilityCount * 2u
        + __hidden_slang_compiler::s_MaxOptimizationArgumentCount
    ;
    if(
        AddOverflows<usize>(maxFixedArgumentCount, defineCount)
        || includeDirectoryCount > (Limit<usize>::s_Max - maxFixedArgumentCount - defineCount) / 2u
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: compiler request for '{}' has too many arguments"), StringConvert(request.shaderName));
        return false;
    }

    const usize argumentReserve = maxFixedArgumentCount + includeDirectoryCount * 2u + defineCount;
    const usize ownedArgumentCount = 2u + includeDirectoryCount + defineCount;
    __hidden_slang_compiler::ScratchVector<AStringView> arguments(argumentArena);
    arguments.reserve(argumentReserve);
    __hidden_slang_compiler::ScratchVector<__hidden_slang_compiler::ScratchString> ownedArguments(argumentArena);
    // Reserve owners before publishing views so inline string storage cannot move.
    ownedArguments.reserve(ownedArgumentCount);
    arguments.push_back(AStringView(NWB_SLANGC_EXECUTABLE));

    ownedArguments.push_back(PathToString<char>(argumentArena, *compilerSourcePath));
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
    for(const Path& includeDirectory : compilerIncludeDirectories){
        arguments.push_back(__hidden_slang_compiler::s_IncludeArgument);
        ownedArguments.push_back(PathToString<char>(argumentArena, includeDirectory));
        arguments.push_back(AStringView(ownedArguments.back()));
    }

    for(u32 i = 0u; i < request.defineCount; ++i){
        const ShaderCook::ShaderMacroDefinition& define = request.defines[i];
        ownedArguments.emplace_back(__hidden_slang_compiler::s_DefineArgument, argumentArena);
        __hidden_slang_compiler::ScratchString& defineArgument = ownedArguments.back();
        defineArgument += define.name;
        if(!define.value.empty()){
            defineArgument += '=';
            const bool plannedMacroInclude = request.compilerInputsHaveBom
                && FindIf(request.externallyPlannedMacroIncludes.begin(), request.externallyPlannedMacroIncludes.end(), [&define](const AStringView name)noexcept{
                    return name == define.name;
                }) != request.externallyPlannedMacroIncludes.end();
            const AStringView includeName = plannedMacroInclude ? UnquoteDoubleQuotedView(define.value) : AStringView();
            const Path includePath(request.sourcePath.arena(), includeName);
            if(!includeName.empty() && includePath.isAbsolute()){
                auto absoluteIncludePath = AbsolutePath(includePath);
                if(absoluteIncludePath)
                    *absoluteIncludePath = absoluteIncludePath->lexicallyNormal();
                const auto overlayIncludePath = absoluteIncludePath ? __hidden_slang_compiler::BuildCompilerOverlayPath(compilerOverlayRoot, *absoluteIncludePath) : Expected<Path>(MakeUnexpected(Failure{}));
                if(!absoluteIncludePath || !__hidden_slang_compiler::IsCompilerDependency(request.dependencies, *absoluteIncludePath) || !overlayIncludePath){
                    NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: planned macro include '{}' is not a mapped compiler dependency"), StringConvert(includeName));
                    return false;
                }
                defineArgument += '"';
                defineArgument += PathToString<char>(argumentArena, *overlayIncludePath);
                defineArgument += '"';
            }
            else
                defineArgument += define.value;
        }
        arguments.push_back(AStringView(defineArgument));
    }

    arguments.push_back(__hidden_slang_compiler::s_OutputArgument);
    ownedArguments.push_back(PathToString<char>(argumentArena, compilerOutputPath));
    arguments.push_back(AStringView(ownedArguments.back()));

    const __hidden_slang_compiler::ScratchString diagnosticsPathText = PathToString<char>(argumentArena, diagnosticsPath);
    const auto exitCode = ::RunProcessRedirectedToFile(
        argumentArena,
        arguments,
        AStringView(diagnosticsPathText)
    );
    if(!exitCode || *exitCode != 0){
        __hidden_slang_compiler::ScratchString diagnostics{argumentArena};
        if(__hidden_slang_compiler::ReadDiagnostics(diagnosticsPath, diagnostics) && !diagnostics.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Shader compile failed for '{}' (variant '{}') :\n{}")
                , StringConvert(request.shaderName)
                , StringConvert(request.variantName)
                , StringConvert(diagnostics)
            );
        }
        else if(exitCode){
            NWB_LOGGER_ERROR(NWB_TEXT("Shader compile failed for '{}' (variant '{}') with exit code {}")
                , StringConvert(request.shaderName)
                , StringConvert(request.variantName)
                , *exitCode
            );
        }
        else{
            NWB_LOGGER_ERROR(NWB_TEXT("Shader compile failed for '{}' (variant '{}'): process execution failure {}")
                , StringConvert(request.shaderName)
                , StringConvert(request.variantName)
                , static_cast<u32>(exitCode.error())
            );
        }
        return false;
    }

    __hidden_slang_compiler::ScratchString diagnostics{argumentArena};
    if(!__hidden_slang_compiler::ReadDiagnostics(diagnosticsPath, diagnostics)){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to read compiler diagnostics '{}' for '{}' (variant '{}')")
            , PathToString<tchar>(diagnosticsPath)
            , StringConvert(request.shaderName)
            , StringConvert(request.variantName)
        );
        return false;
    }
    if(!diagnostics.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Shader compiler emitted unexpected diagnostics for '{}' (variant '{}') :\n{}")
            , StringConvert(request.shaderName)
            , StringConvert(request.variantName)
            , StringConvert(diagnostics)
        );
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
            NWB_LOGGER_ERROR(NWB_TEXT("Shader compile failed for '{}' (variant '{}') : failed to read output '{}'")
                , StringConvert(request.shaderName)
                , StringConvert(request.variantName)
                , PathToString<tchar>(compilerOutputPath)
            );
        }
        return false;
    }

    const Core::ShaderType::Enum shaderType = Core::ShaderStageNames::ShaderTypeFromArchiveStageName(Name(request.stage));
    if(!Core::ResolveSpirvEntryPointName(
        BinaryByteView{ outBytecode.data(), outBytecode.size() },
        request.entryPoint,
        Core::ShaderType::ToMask(shaderType)
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("Shader compile failed for '{}' (variant '{}'): malformed SPIR-V or wrong physical stage/entry point")
            , StringConvert(request.shaderName)
            , StringConvert(request.variantName)
        );
        outBytecode.clear();
        return false;
    }

    auto renamePathResult = RenamePath(compilerOutputPath, request.outputPath);
    if(!renamePathResult){
        __hidden_slang_compiler::ScratchVector<u8> publishedBytecode(argumentArena);
        if(
            ReadBinaryFile(request.outputPath, publishedBytecode)
            && publishedBytecode.size() == outBytecode.size()
            && NWB_MEMCMP(publishedBytecode.data(), outBytecode.data(), outBytecode.size()) == 0
            && Core::ResolveSpirvEntryPointName(
                BinaryByteView{ publishedBytecode.data(), publishedBytecode.size() },
                request.entryPoint,
                Core::ShaderType::ToMask(shaderType)
            )
        )
            return true;
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to publish verified bytecode cache '{}' : {}")
            , PathToString<tchar>(request.outputPath)
            , StringConvert(renamePathResult.error().message())
        );
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

