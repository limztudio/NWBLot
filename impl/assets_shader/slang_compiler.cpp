// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "slang_compiler.h"
#include "source_dependencies.h"

#include "arena_names.h"

#include <impl/assets_material/shader_stage_names.h>

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
    "-profile", MaterialShaderStageNames::s_Spirv15TargetProfileText,
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
static bool ReadDiagnostics(const Path& diagnosticsPath, StringT& outDiagnostics){
    ErrorCode errorCode;
    return ReadBinaryFile(diagnosticsPath, outDiagnostics, errorCode);
}

class ScopedDirectoryCleanupGuard final : NoCopy{
public:
    explicit ScopedDirectoryCleanupGuard(const Path& path)noexcept
        : m_path(path)
    {}
    ~ScopedDirectoryCleanupGuard(){
        ErrorCode errorCode;
        if(RemoveAllIfExists(m_path, errorCode))
            return;

        NWB_LOGGER_WARNING(NWB_TEXT("ShaderCook: failed to remove temporary compiler directory '{}' : {}")
            , PathToString<tchar>(m_path)
            , StringConvert(errorCode.message())
        );
    }


private:
    const Path& m_path;
};

Atomic<u64> g_TemporarySequence{0u};

static bool CreateCompilerWorkDirectory(const Path& parentDirectory, Path& outDirectory){
    ErrorCode errorCode;
    if(!EnsureDirectories(parentDirectory, errorCode)){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to create compiler temporary parent '{}' : {}")
            , PathToString<tchar>(parentDirectory)
            , StringConvert(errorCode.message())
        );
        return false;
    }

    for(;;){
        const u64 sequence = g_TemporarySequence.fetch_add(1u, MemoryOrder::relaxed);
        if(sequence == Limit<u64>::s_Max){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: compiler temporary sequence overflow"));
            return false;
        }
        ShaderCook::CookString directoryName(".nwb_shader_", outDirectory.arena());
        AppendHexU64(CurrentProcessId(), directoryName);
        directoryName += '_';
        AppendHexU64(sequence, directoryName);
        outDirectory = parentDirectory / directoryName;
        if(CreateDirectories(outDirectory, errorCode))
            return true;
        if(errorCode){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to create isolated compiler directory '{}' : {}")
                , PathToString<tchar>(outDirectory)
                , StringConvert(errorCode.message())
            );
            return false;
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool BuildCompilerOverlayPath(const Path& overlayRoot, const Path& absolutePath, Path& outPath){
    outPath.clear();
    if(!absolutePath.isAbsolute())
        return false;

    const auto rootIt = absolutePath.begin();
    if(rootIt == absolutePath.end())
        return false;

    const Path rootPath(absolutePath.arena(), (*rootIt).native());
    const Path relativePath = absolutePath.lexicallyRelative(rootPath);
    if(relativePath.empty())
        return false;

    const ShaderCook::CookString rootText = PathToString<char>(outPath.arena(), rootPath);
    if(rootText.size() > (Limit<usize>::s_Max - 5u) / 2u)
        return false;
    ShaderCook::CookString rootKey("root_", outPath.arena());
    rootKey.reserve(5u + rootText.size() * 2u);
    static constexpr AStringView s_HexDigits = "0123456789abcdef";
    for(const char ch : rootText){
        const u8 byte = static_cast<u8>(ch);
        rootKey += s_HexDigits[byte >> s_HexNibbleBits];
        rootKey += s_HexDigits[byte & s_HexNibbleMask];
    }
    outPath = overlayRoot / rootKey / relativePath;
    return true;
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
        AStringView includeName;
        ShaderSourceDependencies::IncludeKind::Enum includeKind;
        if(
            !ShaderSourceDependencies::ExtractIncludeDirective(scanLine, includeName, includeKind)
            || includeKind == ShaderSourceDependencies::IncludeKind::Macro
        ){
            rewrittenSource.append(line.data(), line.size());
        }
        else{
            if(includeKind == ShaderSourceDependencies::IncludeKind::Unsupported){
                NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: unsupported dependency directive '{}' in '{}'"), StringConvert(TrimView(scanLine)), PathToString<tchar>(sourcePath));
                return false;
            }
            Path includePath(sourcePath.arena(), includeName);
            if(!includePath.isAbsolute()){
                rewrittenSource.append(line.data(), line.size());
            }
            else{
                ErrorCode errorCode;
                const Path absoluteIncludePath = AbsolutePath(includePath, errorCode).lexicallyNormal();
                Path overlayIncludePath(sourcePath.arena());
                if(
                    errorCode
                    || !IsCompilerDependency(request.dependencies, absoluteIncludePath)
                    || !BuildCompilerOverlayPath(overlayRoot, absoluteIncludePath, overlayIncludePath)
                ){
                    NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: absolute include '{}' from '{}' is not a mapped compiler dependency")
                        , StringConvert(includeName)
                        , PathToString<tchar>(sourcePath)
                    );
                    return false;
                }
                const usize includeNameOffset = static_cast<usize>(includeName.data() - scanLine.data());
                const ScratchString overlayIncludeText = PathToString<char>(scratchArena, overlayIncludePath);
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


static bool PrepareBomStrippedCompilerInputs(
    const ShaderCook::ShaderCompilerRequest& request,
    const Path& overlayRoot,
    Path& outSourcePath,
    ScratchVector<Path>& outIncludeDirectories,
    Alloc::ScratchArena& scratchArena
){
    outSourcePath = request.sourcePath;
    outIncludeDirectories.clear();

    if(request.dependencies.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: compiler request for '{}' has no resolved source dependencies"), StringConvert(request.shaderName));
        return false;
    }

    if(!request.compilerInputsHaveBom){
        outIncludeDirectories.reserve(request.includeDirectories.size());
        for(const Path& includeDirectory : request.includeDirectories)
            outIncludeDirectories.push_back(includeDirectory);
        return true;
    }

    ErrorCode errorCode;
    if(!EnsureDirectories(overlayRoot, errorCode)){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to create temporary compiler source directory '{}' : {}")
            , PathToString<tchar>(overlayRoot)
            , StringConvert(errorCode.message())
        );
        return false;
    }

    ScratchString sourceText{scratchArena};
    // Encode each absolute root separately and preserve nested relative includes within that root.
    for(const Path& dependency : request.dependencies){
        Path overlayDependencyPath(dependency.arena());
        if(!BuildCompilerOverlayPath(overlayRoot, dependency, overlayDependencyPath)){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to map compiler input '{}' into the temporary source directory")
                , PathToString<tchar>(dependency)
            );
            return false;
        }

        errorCode.clear();
        if(!EnsureDirectories(overlayDependencyPath.parentPath(), errorCode)){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to create temporary compiler source parent '{}' : {}")
                , PathToString<tchar>(overlayDependencyPath.parentPath())
                , StringConvert(errorCode.message())
            );
            return false;
        }

        sourceText.clear();
        if(!ReadTextFile(dependency, sourceText)){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to read compiler input '{}' while writing BOM-stripped source")
                , PathToString<tchar>(dependency)
            );
            return false;
        }
        StripUtf8Bom(sourceText);
        if(!RewriteAbsoluteCompilerIncludes(sourceText, dependency, request, overlayRoot, scratchArena))
            return false;
        if(!WriteTextFile(overlayDependencyPath, AStringView(sourceText.data(), sourceText.size()))){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to write BOM-stripped compiler input '{}'"), PathToString<tchar>(overlayDependencyPath));
            return false;
        }
    }

    ErrorCode sourcePathError;
    const Path absoluteSourcePath = AbsolutePath(request.sourcePath, sourcePathError).lexicallyNormal();
    if(sourcePathError || !IsCompilerDependency(request.dependencies, absoluteSourcePath) || !BuildCompilerOverlayPath(overlayRoot, absoluteSourcePath, outSourcePath)){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to map source '{}' into the temporary compiler source directory")
            , PathToString<tchar>(request.sourcePath)
        );
        return false;
    }

    if(request.includeDirectories.size() > Limit<usize>::s_Max / 2u){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: compiler request for '{}' has too many include directories"), StringConvert(request.shaderName));
        return false;
    }

    outIncludeDirectories.reserve(request.includeDirectories.size() * 2u);
    for(const Path& includeDirectory : request.includeDirectories){
        errorCode.clear();
        const Path absoluteIncludeDirectory = AbsolutePath(includeDirectory, errorCode).lexicallyNormal();
        Path overlayIncludeDirectory(includeDirectory.arena());
        if(errorCode || !BuildCompilerOverlayPath(overlayRoot, absoluteIncludeDirectory, overlayIncludeDirectory)){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to map include directory '{}' into the temporary compiler source directory")
                , PathToString<tchar>(includeDirectory)
            );
            return false;
        }
        outIncludeDirectories.push_back(Move(overlayIncludeDirectory));
    }
    for(const Path& includeDirectory : request.includeDirectories)
        outIncludeDirectories.push_back(includeDirectory);

    return true;
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


static bool TryMapStageToSlangStage(const AStringView stage, AStringView& outStage)noexcept{
    for(const SlangStageMapping& mapping : s_StageMappings){
        if(stage == mapping.stage){
            outStage = mapping.slangStage;
            return true;
        }
    }

    outStage = {};
    return false;
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


bool SlangShaderCompiler::ComputeCompilerFingerprint(
    const Path& temporaryRoot,
    u64& outFingerprint,
    Alloc::ScratchArena& scratchArena
){
    outFingerprint = s_Fnv64OffsetBasis;
    const Path compilerPath(temporaryRoot.arena(), NWB_SLANGC_EXECUTABLE);
    InputFileStream compilerStream(compilerPath, s_FileOpenBinary);
    if(!compilerStream.is_open()){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to open configured compiler '{}' for fingerprinting"), PathToString<tchar>(compilerPath));
        return false;
    }

    Array<char, 8192u> compilerBytes{};
    for(;;){
        compilerStream.read(compilerBytes.data(), static_cast<StreamSize>(compilerBytes.size()));
        const StreamSize bytesRead = compilerStream.gcount();
        if(bytesRead > 0)
            outFingerprint = UpdateFnv64(outFingerprint, reinterpret_cast<const u8*>(compilerBytes.data()), static_cast<usize>(bytesRead));
        if(compilerStream.eof() && !compilerStream.bad())
            break;
        if(!compilerStream.good()){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to read configured compiler '{}' for fingerprinting"), PathToString<tchar>(compilerPath));
            return false;
        }
    }

    Path workDirectory(temporaryRoot.arena());
    if(!__hidden_slang_compiler::CreateCompilerWorkDirectory(temporaryRoot, workDirectory))
        return false;
    __hidden_slang_compiler::ScopedDirectoryCleanupGuard workDirectoryCleanup(workDirectory);
    const Path versionPath = workDirectory / "compiler.version";
    const __hidden_slang_compiler::ScratchString versionPathText = PathToString<char>(scratchArena, versionPath);
    const AStringView arguments[]{ AStringView(NWB_SLANGC_EXECUTABLE), "-version" };
    bool exitCodeQueryFailed = false;
    const int exitCode = RunProcessRedirectedToFile(scratchArena, arguments, AStringView(versionPathText), &exitCodeQueryFailed);
    __hidden_slang_compiler::ScratchString versionText(scratchArena);
    if(exitCodeQueryFailed || exitCode != 0 || !__hidden_slang_compiler::ReadDiagnostics(versionPath, versionText) || TrimView(versionText).empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to query configured compiler version '{}'"), PathToString<tchar>(compilerPath));
        return false;
    }

    const auto appendText = [&outFingerprint](const AStringView text)noexcept{
        Fnv64AppendBuffer(outFingerprint, reinterpret_cast<const u8*>(text.data()), text.size());
    };
    // The version identifies the loaded Slang compiler library as well as its executable launcher.
    appendText(TrimView(versionText));
    for(const AStringView argument : __hidden_slang_compiler::s_CommonCompilerArguments)
        appendText(argument);
    for(const AStringView capability : __hidden_slang_compiler::s_SpirvBaselineCapabilities){
        appendText(__hidden_slang_compiler::s_CapabilityArgument);
        appendText(capability);
    }
    appendText(MaterialShaderStageNames::s_SpvRayQueryCapabilityText);
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
    return true;
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

    AStringView slangStage;
    if(!__hidden_slang_compiler::TryMapStageToSlangStage(request.stage, slangStage)){
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
    Path compilerWorkDirectory(request.outputPath.arena());
    if(!__hidden_slang_compiler::CreateCompilerWorkDirectory(outputParent.empty() ? Path(outputParent.arena(), ".") : outputParent, compilerWorkDirectory))
        return false;
    __hidden_slang_compiler::ScopedDirectoryCleanupGuard workDirectoryCleanup(compilerWorkDirectory);
    const Path diagnosticsPath = compilerWorkDirectory / "compiler.diag";
    const Path compilerOutputPath = compilerWorkDirectory / "module.spv";
    const Path compilerOverlayRoot = compilerWorkDirectory / "sources";
    Path compilerSourcePath(request.sourcePath.arena());
    __hidden_slang_compiler::ScratchVector<Path> compilerIncludeDirectories(argumentArena);
    if(!__hidden_slang_compiler::PrepareBomStrippedCompilerInputs(
        request,
        compilerOverlayRoot,
        compilerSourcePath,
        compilerIncludeDirectories,
        argumentArena
    ))
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

    ownedArguments.push_back(PathToString<char>(argumentArena, compilerSourcePath));
    arguments.push_back(AStringView(ownedArguments.back()));
    for(const AStringView argument : __hidden_slang_compiler::s_CommonCompilerArguments)
        arguments.push_back(argument);
    if(!optimizationArgument.empty())
        arguments.push_back(optimizationArgument);
    if(request.rayQuery){
        arguments.push_back(__hidden_slang_compiler::s_CapabilityArgument);
        arguments.push_back(MaterialShaderStageNames::s_SpvRayQueryCapabilityText);
    }
    for(const AStringView capability : __hidden_slang_compiler::s_SpirvBaselineCapabilities){
        arguments.push_back(__hidden_slang_compiler::s_CapabilityArgument);
        arguments.push_back(capability);
    }
    arguments.push_back(__hidden_slang_compiler::s_EntryPointArgument);
    arguments.push_back(request.entryPoint);
    arguments.push_back(__hidden_slang_compiler::s_StageArgument);
    arguments.push_back(slangStage);
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
                ErrorCode errorCode;
                const Path absoluteIncludePath = AbsolutePath(includePath, errorCode).lexicallyNormal();
                Path overlayIncludePath(request.sourcePath.arena());
                if(errorCode || !__hidden_slang_compiler::IsCompilerDependency(request.dependencies, absoluteIncludePath)
                    || !__hidden_slang_compiler::BuildCompilerOverlayPath(compilerOverlayRoot, absoluteIncludePath, overlayIncludePath)){
                    NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: planned macro include '{}' is not a mapped compiler dependency"), StringConvert(includeName));
                    return false;
                }
                defineArgument += '"';
                defineArgument += PathToString<char>(argumentArena, overlayIncludePath);
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
    bool exitCodeQueryFailed = false;
    const int exitCode = ::RunProcessRedirectedToFile(
        argumentArena,
        arguments,
        AStringView(diagnosticsPathText),
        &exitCodeQueryFailed
    );
    if(exitCodeQueryFailed || exitCode != 0){
        __hidden_slang_compiler::ScratchString diagnostics{argumentArena};
        if(__hidden_slang_compiler::ReadDiagnostics(diagnosticsPath, diagnostics) && !diagnostics.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Shader compile failed for '{}' (variant '{}') :\n{}")
                , StringConvert(request.shaderName)
                , StringConvert(request.variantName)
                , StringConvert(diagnostics)
            );
        }
        else{
            NWB_LOGGER_ERROR(NWB_TEXT("Shader compile failed for '{}' (variant '{}') with exit code {}")
                , StringConvert(request.shaderName)
                , StringConvert(request.variantName)
                , exitCode
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

    ErrorCode errorCode;
    if(!ReadBinaryFile(compilerOutputPath, outBytecode, errorCode)){
        if(errorCode){
            NWB_LOGGER_ERROR(NWB_TEXT("Shader compile failed for '{}' (variant '{}') : failed to read output '{}' : {}")
                , StringConvert(request.shaderName)
                , StringConvert(request.variantName)
                , PathToString<tchar>(compilerOutputPath)
                , StringConvert(errorCode.message())
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
    AStringView validatedEntryPoint;
    if(Core::ResolveSpirvEntryPointName(
        BinaryByteView{ outBytecode.data(), outBytecode.size() },
        request.entryPoint,
        Core::ShaderType::ToMask(shaderType),
        validatedEntryPoint
    ) != Core::SpirvEntryPointLookupResult::Found){
        NWB_LOGGER_ERROR(NWB_TEXT("Shader compile failed for '{}' (variant '{}'): malformed SPIR-V or wrong physical stage/entry point")
            , StringConvert(request.shaderName)
            , StringConvert(request.variantName)
        );
        outBytecode.clear();
        return false;
    }

    if(!RenamePath(compilerOutputPath, request.outputPath, errorCode)){
        __hidden_slang_compiler::ScratchVector<u8> publishedBytecode(argumentArena);
        ErrorCode readError;
        AStringView publishedEntryPoint;
        if(
            ReadBinaryFile(request.outputPath, publishedBytecode, readError)
            && publishedBytecode.size() == outBytecode.size()
            && NWB_MEMCMP(publishedBytecode.data(), outBytecode.data(), outBytecode.size()) == 0
            && Core::ResolveSpirvEntryPointName(
                BinaryByteView{ publishedBytecode.data(), publishedBytecode.size() },
                request.entryPoint,
                Core::ShaderType::ToMask(shaderType),
                publishedEntryPoint
            ) == Core::SpirvEntryPointLookupResult::Found
        )
            return true;
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to publish verified bytecode cache '{}' : {}")
            , PathToString<tchar>(request.outputPath)
            , StringConvert(errorCode.message())
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

