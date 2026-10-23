// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "slang_compiler.h"
#include "slang_compiler_workspace.h"
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


using ScratchPath = SlangCompilerWorkspace::ScratchPath;
using ScratchString = SlangCompilerWorkspace::ScratchString;
template<typename T>
using ScratchVector = SlangCompilerWorkspace::ScratchVector<T>;


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
static constexpr usize s_FixedOwnedArgumentCount = 2u;
static constexpr usize s_FingerprintReadBufferBytes = 8192u;
static constexpr usize s_QuotePairByteCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
        overlayIncludePath = SlangCompilerWorkspace::BuildCompilerOverlayPath(overlayRoot, normalizedIncludePath, scratchArena);
        if(!SlangCompilerWorkspace::IsCompilerDependency(request.dependencies, normalizedIncludePath) || !overlayIncludePath){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: planned macro include '{}' is not a mapped compiler dependency"), StringConvert(includeName));
            return MakeUnexpected(Failure{});
        }
        const auto capacity = SlangCompilerWorkspace::CompilerPathTextCapacity(*overlayIncludePath);
        if(!capacity || *capacity > Limit<usize>::s_Max - s_QuotePairByteCount)
            return MakeUnexpected(Failure{});
        valueCapacity = *capacity + s_QuotePairByteCount;
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

    Array<char, __hidden_slang_compiler::s_FingerprintReadBufferBytes> compilerBytes{};
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

    const auto workDirectoryResult = SlangCompilerWorkspace::CreateCompilerWorkDirectory(__hidden_slang_compiler::ScratchPath(scratchArena, temporaryRoot), scratchArena);
    if(!workDirectoryResult)
        return MakeUnexpected(Failure{});
    const __hidden_slang_compiler::ScratchPath& workDirectory = *workDirectoryResult;
    SlangCompilerWorkspace::ScopedDirectoryCleanupGuard workDirectoryCleanup(workDirectory);
    const __hidden_slang_compiler::ScratchPath versionPath = workDirectory / "compiler.version";
    const auto versionPathText = SlangCompilerWorkspace::BuildCompilerPathText(versionPath, scratchArena);
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


bool SlangShaderCompiler::CompileVariant(const ShaderCook::ShaderCompilerRequest& request, Core::Alloc::ScratchArena& scratchArena, ShaderCook::CookVector<u8>& outBytecode){
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
    const auto compilerWorkDirectoryResult = SlangCompilerWorkspace::CreateCompilerWorkDirectory(outputParent.empty() ? __hidden_slang_compiler::ScratchPath(scratchArena, ".") : outputParent, scratchArena);
    if(!compilerWorkDirectoryResult)
        return false;
    const __hidden_slang_compiler::ScratchPath& compilerWorkDirectory = *compilerWorkDirectoryResult;
    SlangCompilerWorkspace::ScopedDirectoryCleanupGuard workDirectoryCleanup(compilerWorkDirectory);
    const __hidden_slang_compiler::ScratchPath diagnosticsPath = compilerWorkDirectory / "compiler.diag";
    const __hidden_slang_compiler::ScratchPath compilerOutputPath = compilerWorkDirectory / "module.spv";
    const __hidden_slang_compiler::ScratchPath compilerOverlayRoot = compilerWorkDirectory / "sources";
    const auto compilerInputs = SlangCompilerWorkspace::PrepareBomStrippedCompilerInputs(request, compilerOverlayRoot, scratchArena);
    if(!compilerInputs)
        return false;
    const __hidden_slang_compiler::ScratchVector<__hidden_slang_compiler::ScratchPath>& compilerIncludeDirectories = compilerInputs->includeDirectories;

    const usize includeDirectoryCount = compilerIncludeDirectories.size();
    const usize defineCount = static_cast<usize>(request.defineCount);
    constexpr usize s_MaxFixedArgumentCount = __hidden_slang_compiler::s_BaseCompilerArgumentCount + __hidden_slang_compiler::s_MaxTargetProfileCapabilityCount * 2u + __hidden_slang_compiler::s_MaxOptimizationArgumentCount;
    if(AddOverflows<usize>(s_MaxFixedArgumentCount, defineCount) || includeDirectoryCount > (Limit<usize>::s_Max - s_MaxFixedArgumentCount - defineCount) / 2u){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: compiler request for '{}' has too many arguments"), StringConvert(request.shaderName));
        return false;
    }

    const usize argumentReserve = s_MaxFixedArgumentCount + includeDirectoryCount * 2u + defineCount;
    const usize ownedArgumentCount = __hidden_slang_compiler::s_FixedOwnedArgumentCount + includeDirectoryCount + defineCount;
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

    auto sourcePathText = SlangCompilerWorkspace::BuildCompilerPathText(compilerInputs->sourcePath, scratchArena);
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
        auto includeDirectoryText = SlangCompilerWorkspace::BuildCompilerPathText(includeDirectory, scratchArena);
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
    auto outputPathText = SlangCompilerWorkspace::BuildCompilerPathText(compilerOutputPath, scratchArena);
    if(!outputPathText)
        return false;
    ownedArguments.push_back(Move(*outputPathText));
    arguments.push_back(AStringView(ownedArguments.back()));

    const auto diagnosticsPathText = SlangCompilerWorkspace::BuildCompilerPathText(diagnosticsPath, scratchArena);
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

