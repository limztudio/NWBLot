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


using namespace ShaderBinaryPayload;
using ScratchPath = SlangCompilerWorkspace::ScratchPath;
using ScratchString = SlangCompilerWorkspace::ScratchString;
template<typename T>
using ScratchVector = SlangCompilerWorkspace::ScratchVector<T>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Slang's SPIR-V 1.5 profile does not include the standard capability set used by the generated Vulkan shaders.
// Declare it explicitly so Slang produces the same output without silently rewriting the requested profile.
static constexpr Array<AStringView, s_SpirvBaselineCapabilityCount> s_SpirvBaselineCapabilities = {
    s_SpirvCapabilityNonSemanticInfo,
    s_SpirvCapabilityUserType,
    s_SpirvCapabilityDerivativeControl,
    s_SpirvCapabilityImageQuery,
    s_SpirvCapabilityImageGatherExtended,
    s_SpirvCapabilitySparseResidency,
    s_SpirvCapabilityMinLod,
    s_SpirvCapabilityFragmentFullyCovered,
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr Array<AStringView, s_CommonCompilerArgumentCount> s_CommonCompilerArguments = {
    s_SlangTargetArgument, s_SlangTargetSpirv,
    s_SlangEmitSpirvDirectlyArgument,
    s_SlangUseEntryPointNameArgument,
    s_SlangWarningsAsErrorsArgument, s_SlangWarningsAsErrorsAll,
    s_SlangProfileArgument, ShaderBinaryPayload::s_Spirv15TargetProfileText,
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<ScratchString> BuildCompilerDefineArgument(const ShaderBinaryPayload::ShaderMacroDefinition& define, const ShaderCook::ShaderCompilerRequest& request, const ScratchPath& overlayRoot, Alloc::ScratchArena& scratchArena){
    const b8 plannedMacroInclude = request.compilerInputsHaveBom && FindIf(request.externallyPlannedMacroIncludes.begin(), request.externallyPlannedMacroIncludes.end(), [&define](const AStringView name)noexcept{
            return name == define.name;
        }) != request.externallyPlannedMacroIncludes.end();
    const AStringView includeName = plannedMacroInclude ? UnquoteDoubleQuotedView(define.value) : AStringView();
    const ScratchPath includePath(scratchArena, includeName);
    Expected<ScratchPath> overlayIncludePath = MakeUnexpected(Failure{});
    usize valueCapacity = define.value.size();
    if(!includeName.empty() && includePath.isAbsolute()){
        const auto absoluteIncludePath = AbsolutePath(includePath);
        if(!absoluteIncludePath){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to resolve planned macro include '{}': {}:{}"), PathToString<tchar>(includePath), ConvertString(absoluteIncludePath.error().category().name()), absoluteIncludePath.error().value());
            return MakeUnexpected(Failure{});
        }
        const ScratchPath normalizedIncludePath = absoluteIncludePath->lexicallyNormal();
        overlayIncludePath = SlangCompilerWorkspace::BuildCompilerOverlayPath(overlayRoot, normalizedIncludePath, scratchArena);
        if(!SlangCompilerWorkspace::IsCompilerDependency(request.dependencies, normalizedIncludePath) || !overlayIncludePath){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: planned macro include '{}' is not a mapped compiler dependency"), ConvertString(includeName));
            return MakeUnexpected(Failure{});
        }
        const auto capacity = SlangCompilerWorkspace::CompilerPathTextCapacity(*overlayIncludePath);
        if(!capacity || *capacity > Limit<usize>::s_Max - s_SlangQuotePairByteCount)
            return MakeUnexpected(Failure{});
        valueCapacity = *capacity + s_SlangQuotePairByteCount;
    }
    const usize fixedBytes = s_SlangDefineArgument.size() + (!define.value.empty() ? 1u : 0u);
    if(define.name.size() > Limit<usize>::s_Max - fixedBytes || valueCapacity > Limit<usize>::s_Max - fixedBytes - define.name.size())
        return MakeUnexpected(Failure{});
    ScratchString argument{scratchArena};
    argument.reserve(fixedBytes + define.name.size() + valueCapacity);
    argument += s_SlangDefineArgument;
    argument += define.name;
    if(!define.value.empty()){
        argument += '=';
        if(overlayIncludePath){
            argument += '"';
            auto out = BackInserter(argument);
            WriteConvertedText<char>(out, overlayIncludePath->native());
            argument += '"';
        }
        else
            argument += define.value;
    }
    return argument;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static const Array<SlangStageMapping, 14u> s_StageMappings = {{
    { Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::VertexStage), s_SlangStageVertex },
    { Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::HullStage), s_SlangStageHull },
    { Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::DomainStage), s_SlangStageDomain },
    { Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::GeometryStage), s_SlangStageGeometry },
    { Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::PixelStage), s_SlangStageFragment },
    { Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::ComputeStage), s_SlangStageCompute },
    { Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::AmplificationStage), s_SlangStageAmplification },
    { Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::MeshStage), s_SlangStageMesh },
    { Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::RayGenerationStage), s_SlangStageRayGeneration },
    { Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::AnyHitStage), s_SlangStageAnyHit },
    { Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::ClosestHitStage), s_SlangStageClosestHit },
    { Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::MissStage), s_SlangStageMiss },
    { Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::IntersectionStage), s_SlangStageIntersection },
    { Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::CallableStage), s_SlangStageCallable },
}};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<AStringView> TryMapStageToSlangStage(const AStringView stage)noexcept{
    for(const SlangStageMapping& mapping : s_StageMappings){
        if(stage == mapping.stage)
            return mapping.slangStage;
    }

    return MakeUnexpected(Failure{});
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<u64> SlangShaderCompiler::ComputeCompilerFingerprint(const Path& temporaryRoot, Alloc::ScratchArena& scratchArena){
    using namespace __hidden_slang_compiler;

    u64 fingerprint = s_Fnv64OffsetBasis;
    const ScratchPath compilerPath(scratchArena, NWB_SLANGC_EXECUTABLE);
    InputFileStream compilerStream(compilerPath, s_FileOpenBinary);
    if(!compilerStream.is_open()){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to open configured compiler '{}' for fingerprinting"), PathToString<tchar>(compilerPath));
        return MakeUnexpected(Failure{});
    }

    Array<char, s_SlangFingerprintReadBufferBytes> compilerBytes{};
    for(;;){
        compilerStream.read(compilerBytes.data(), static_cast<StreamSize>(compilerBytes.size()));
        const StreamSize bytesRead = compilerStream.gcount();
        if(bytesRead > 0)
            fingerprint = UpdateFnv64TextExact(fingerprint, AStringView(compilerBytes.data(), static_cast<usize>(bytesRead)));
        if(compilerStream.eof() && !compilerStream.bad())
            break;
        if(!compilerStream.good()){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to read configured compiler '{}' for fingerprinting"), PathToString<tchar>(compilerPath));
            return MakeUnexpected(Failure{});
        }
    }

    const auto workDirectoryResult = SlangCompilerWorkspace::CreateCompilerWorkDirectory(ScratchPath(scratchArena, temporaryRoot), scratchArena);
    if(!workDirectoryResult)
        return MakeUnexpected(Failure{});
    const ScratchPath& workDirectory = *workDirectoryResult;
    SlangCompilerWorkspace::ScopedDirectoryCleanupGuard workDirectoryCleanup(workDirectory);
    const ScratchPath versionPath = workDirectory / s_SlangVersionFileName;
    const auto versionPathText = SlangCompilerWorkspace::BuildCompilerPathText(versionPath, scratchArena);
    if(!versionPathText)
        return MakeUnexpected(Failure{});
    const Array<AStringView, 2u> arguments = { AStringView(NWB_SLANGC_EXECUTABLE), s_SlangVersionArgument };
    const auto exitCode = RunProcessRedirectedToFile(scratchArena, arguments, AStringView(*versionPathText));
    if(!exitCode){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to execute configured compiler '{}' for version query: process execution failure {}"), PathToString<tchar>(compilerPath), static_cast<u32>(exitCode.error()));
        return MakeUnexpected(Failure{});
    }
    if(*exitCode != 0){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: configured compiler '{}' version query failed with exit code {}"), PathToString<tchar>(compilerPath), *exitCode);
        return MakeUnexpected(Failure{});
    }
    ScratchString versionText(scratchArena);
    const auto versionRead = ReadBinaryFile(versionPath, versionText);
    if(!versionRead){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to read configured compiler version '{}': {}:{}"), PathToString<tchar>(versionPath), ConvertString(versionRead.error().category().name()), versionRead.error().value());
        return MakeUnexpected(Failure{});
    }
    if(TrimView(versionText).empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: configured compiler '{}' returned empty version text"), PathToString<tchar>(compilerPath));
        return MakeUnexpected(Failure{});
    }

    const auto appendText = [&fingerprint](const AStringView text)noexcept{
        fingerprint = UpdateFnv64TextExact(fingerprint, text);
    };
    // The version identifies the loaded Slang compiler library as well as its executable launcher.
    appendText(TrimView(versionText));
    for(const AStringView argument : s_CommonCompilerArguments)
        appendText(argument);
    for(const AStringView capability : s_SpirvBaselineCapabilities){
        appendText(s_SlangCapabilityArgument);
        appendText(capability);
    }
    appendText(ShaderBinaryPayload::s_SpvRayQueryCapabilityText);
    appendText(s_SlangEntryPointArgument);
    appendText(s_SlangStageArgument);
    appendText(s_SlangIncludeArgument);
    appendText(s_SlangDefineArgument);
    appendText(s_SlangOutputArgument);
    for(const SlangStageMapping& mapping : s_StageMappings){
        appendText(mapping.stage);
        appendText(mapping.slangStage);
    }
    for(u8 optimizationLevel = 0u; optimizationLevel < ShaderBinaryPayload::ShaderOptimizationLevel::kCount; ++optimizationLevel)
        appendText(SlangOptimizationArgument(static_cast<ShaderBinaryPayload::ShaderOptimizationLevel::Enum>(optimizationLevel)));
    return fingerprint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 SlangShaderCompiler::CompileVariant(const ShaderCook::ShaderCompilerRequest& request, Core::Alloc::ScratchArena& scratchArena, ShaderCook::CookVector<u8>& outBytecode){
    using namespace __hidden_slang_compiler;

    outBytecode.clear();

    if(request.sourcePath.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to compile shader '{}' : source path is empty"), ConvertString(request.shaderName));
        return false;
    }

    if(request.outputPath.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to compile shader '{}' : output path is empty"), ConvertString(request.shaderName));
        return false;
    }

    if(request.entryPoint.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to compile shader '{}' : entry point is empty"), ConvertString(request.shaderName));
        return false;
    }

    const auto slangStage = TryMapStageToSlangStage(request.stage);
    if(!slangStage){
        NWB_LOGGER_ERROR(NWB_TEXT("Unknown shader stage '{}' in entry '{}'"), ConvertString(request.stage), ConvertString(request.shaderName));
        return false;
    }

    const AStringView optimizationArgument = SlangOptimizationArgument(request.optimizationLevel);
    if(request.optimizationLevel >= ShaderBinaryPayload::ShaderOptimizationLevel::kCount){
        NWB_LOGGER_ERROR(NWB_TEXT("Shader '{}' uses an invalid optimization level {}"), ConvertString(request.shaderName), static_cast<u32>(request.optimizationLevel));
        return false;
    }

    const ScratchPath outputParent = request.outputPath.parentPath();
    const auto compilerWorkDirectoryResult = SlangCompilerWorkspace::CreateCompilerWorkDirectory(outputParent.empty() ? ScratchPath(scratchArena, s_CurrentDirectoryPathText) : outputParent, scratchArena);
    if(!compilerWorkDirectoryResult)
        return false;
    const ScratchPath& compilerWorkDirectory = *compilerWorkDirectoryResult;
    SlangCompilerWorkspace::ScopedDirectoryCleanupGuard workDirectoryCleanup(compilerWorkDirectory);
    const ScratchPath diagnosticsPath = compilerWorkDirectory / s_CompilerDiagFileName;
    const ScratchPath compilerOutputPath = compilerWorkDirectory / s_ModuleSpvFileName;
    const ScratchPath compilerOverlayRoot = compilerWorkDirectory / s_SourcesDirName;
    const auto compilerInputs = SlangCompilerWorkspace::PrepareBomStrippedCompilerInputs(request, compilerOverlayRoot, scratchArena);
    if(!compilerInputs)
        return false;
    const ScratchVector<ScratchPath>& compilerIncludeDirectories = compilerInputs->includeDirectories;

    const usize includeDirectoryCount = compilerIncludeDirectories.size();
    const usize defineCount = request.defines.size();
    if(AddOverflows<usize>(s_MaxFixedArgumentCount, defineCount) || includeDirectoryCount > (Limit<usize>::s_Max - s_MaxFixedArgumentCount - defineCount) / 2u){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: compiler request for '{}' has too many arguments"), ConvertString(request.shaderName));
        return false;
    }

    const usize argumentReserve = s_MaxFixedArgumentCount + includeDirectoryCount * 2u + defineCount;
    const usize ownedArgumentCount = s_SlangFixedOwnedArgumentCount + includeDirectoryCount + defineCount;
    ScratchVector<AStringView> arguments(scratchArena);
    arguments.reserve(argumentReserve);
    ScratchVector<ScratchString> ownedArguments(scratchArena);
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
    for(const AStringView argument : s_CommonCompilerArguments)
        arguments.push_back(argument);
    if(!optimizationArgument.empty())
        arguments.push_back(optimizationArgument);
    if(request.rayQuery){
        arguments.push_back(s_SlangCapabilityArgument);
        arguments.push_back(ShaderBinaryPayload::s_SpvRayQueryCapabilityText);
    }
    for(const AStringView capability : s_SpirvBaselineCapabilities){
        arguments.push_back(s_SlangCapabilityArgument);
        arguments.push_back(capability);
    }
    arguments.push_back(s_SlangEntryPointArgument);
    arguments.push_back(request.entryPoint);
    arguments.push_back(s_SlangStageArgument);
    arguments.push_back(*slangStage);
    for(const ScratchPath& includeDirectory : compilerIncludeDirectories){
        arguments.push_back(s_SlangIncludeArgument);
        auto includeDirectoryText = SlangCompilerWorkspace::BuildCompilerPathText(includeDirectory, scratchArena);
        if(!includeDirectoryText)
            return false;
        ownedArguments.push_back(Move(*includeDirectoryText));
        arguments.push_back(AStringView(ownedArguments.back()));
    }

    for(const ShaderBinaryPayload::ShaderMacroDefinition& define : request.defines){
        auto defineArgument = BuildCompilerDefineArgument(define, request, compilerOverlayRoot, scratchArena);
        if(!defineArgument)
            return false;
        ownedArguments.push_back(Move(*defineArgument));
        arguments.push_back(AStringView(ownedArguments.back()));
    }

    arguments.push_back(s_SlangOutputArgument);
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
        ScratchString diagnostics{scratchArena};
        const auto diagnosticsRead = ReadBinaryFile(diagnosticsPath, diagnostics);
        if(!diagnosticsRead){
            NWB_LOGGER_ERROR(NWB_TEXT("Shader compile failed for '{}' (variant '{}'): {} {}; diagnostics read '{}' failed: {}:{}")
                , ConvertString(request.shaderName)
                , ConvertString(request.variantName)
                , failureStage
                , failureCode
                , PathToString<tchar>(diagnosticsPath)
                , ConvertString(diagnosticsRead.error().category().name())
                , diagnosticsRead.error().value()
            );
        }
        else if(!diagnostics.empty())
            NWB_LOGGER_ERROR(NWB_TEXT("Shader compile failed for '{}' (variant '{}'): {} {} :\n{}"), ConvertString(request.shaderName), ConvertString(request.variantName), failureStage, failureCode, ConvertString(diagnostics));
        else
            NWB_LOGGER_ERROR(NWB_TEXT("Shader compile failed for '{}' (variant '{}'): {} {}"), ConvertString(request.shaderName), ConvertString(request.variantName), failureStage, failureCode);
        return false;
    }

    ScratchString diagnostics{scratchArena};
    const auto diagnosticsRead = ReadBinaryFile(diagnosticsPath, diagnostics);
    if(!diagnosticsRead){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to read compiler diagnostics '{}' for '{}' (variant '{}'): {}:{}")
            , PathToString<tchar>(diagnosticsPath)
            , ConvertString(request.shaderName)
            , ConvertString(request.variantName)
            , ConvertString(diagnosticsRead.error().category().name())
            , diagnosticsRead.error().value()
        );
        return false;
    }
    if(!diagnostics.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Shader compiler emitted unexpected diagnostics for '{}' (variant '{}') :\n{}"), ConvertString(request.shaderName), ConvertString(request.variantName), ConvertString(diagnostics));
        return false;
    }

    auto readBinaryFileResult = ReadBinaryFile(compilerOutputPath, outBytecode);
    if(!readBinaryFileResult){
        if(readBinaryFileResult.error()){
            NWB_LOGGER_ERROR(NWB_TEXT("Shader compile failed for '{}' (variant '{}') : failed to read output '{}' : {}")
                , ConvertString(request.shaderName)
                , ConvertString(request.variantName)
                , PathToString<tchar>(compilerOutputPath)
                , ConvertString(readBinaryFileResult.error().message())
            );
        }
        else
            NWB_LOGGER_ERROR(NWB_TEXT("Shader compile failed for '{}' (variant '{}') : failed to read output '{}'"), ConvertString(request.shaderName), ConvertString(request.variantName), PathToString<tchar>(compilerOutputPath));
        return false;
    }

    const auto stageName = Name::FromText(request.stage);
    if(!stageName){
        NWB_LOGGER_ERROR(NWB_TEXT("Shader compile failed for '{}' (variant '{}'): shader stage contains an embedded null"), ConvertString(request.shaderName), ConvertString(request.variantName));
        outBytecode.clear();
        return false;
    }
    const Core::ShaderType::Enum shaderType = Core::ShaderStageNames::ShaderTypeFromArchiveStageName(*stageName);
    if(!Core::ResolveSpirvEntryPointName(BinaryByteView{ outBytecode.data(), outBytecode.size() }, request.entryPoint, Core::ShaderType::ToMask(shaderType))){
        NWB_LOGGER_ERROR(NWB_TEXT("Shader compile failed for '{}' (variant '{}'): malformed SPIR-V or wrong physical stage/entry point"), ConvertString(request.shaderName), ConvertString(request.variantName));
        outBytecode.clear();
        return false;
    }

    auto renamePathResult = RenamePath(compilerOutputPath, request.outputPath);
    if(!renamePathResult){
        ScratchVector<u8> publishedBytecode(scratchArena);
        if(
            ReadBinaryFile(request.outputPath, publishedBytecode)
            && publishedBytecode.size() == outBytecode.size()
            && NWB_MEMCMP(publishedBytecode.data(), outBytecode.data(), outBytecode.size()) == 0
            && Core::ResolveSpirvEntryPointName(BinaryByteView{ publishedBytecode.data(), publishedBytecode.size() }, request.entryPoint, Core::ShaderType::ToMask(shaderType))
        )
            return true;
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to publish verified bytecode cache '{}' : {}"), PathToString<tchar>(request.outputPath), ConvertString(renamePathResult.error().message()));
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

