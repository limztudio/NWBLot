// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "slang_compiler.h"
#include "slang_compiler_fingerprint.h"

#include <impl/assets_shader/shader_source_fingerprint.adhd.h>
#include <impl/assets_shader/slang_compiler_request_validator.adhd.h>
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
        if(!capacity)
            return MakeUnexpected(Failure{});
        const auto quotedCapacity = ShaderBinaryPayload::TryComputeQuotedCompilerPathCapacity(*capacity);
        if(!quotedCapacity)
            return MakeUnexpected(Failure{});
        valueCapacity = *quotedCapacity;
    }
    const usize fixedBytes = s_SlangDefineArgument.size() + (!define.value.empty() ? 1u : 0u);
    const auto argumentCapacity = ShaderBinaryPayload::TryComputeCompilerDefineArgumentCapacity(fixedBytes, define.name.size(), valueCapacity);
    if(!argumentCapacity)
        return MakeUnexpected(Failure{});
    ScratchString argument{scratchArena};
    argument.reserve(*argumentCapacity);
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


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<u64> SlangShaderCompiler::ComputeCompilerFingerprint(const Path& temporaryRoot, Alloc::ScratchArena& scratchArena){
    using namespace __hidden_slang_compiler;
    return SlangCompilerFingerprint::Compute(temporaryRoot, scratchArena, s_CommonCompilerArguments, s_SpirvBaselineCapabilities, s_StageMappings);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 SlangShaderCompiler::CompileVariant(const ShaderCook::ShaderCompilerRequest& request, Core::Alloc::ScratchArena& scratchArena, ShaderCook::CookVector<u8>& outBytecode){
    using namespace __hidden_slang_compiler;

    outBytecode.clear();

    AStringView slangStage;
    AStringView optimizationArgument;
    if(!SlangCompilerRequestValidator{}.validateInputs(
        request.shaderName,
        request.sourcePath.empty(),
        request.outputPath.empty(),
        request.entryPoint,
        request.stage,
        static_cast<u8>(request.optimizationLevel),
        s_StageMappings,
        slangStage,
        optimizationArgument
    ))
        return false;

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
    usize argumentReserve = 0u;
    usize ownedArgumentCount = 0u;
    if(!SlangCompilerRequestValidator{}.argumentCounts(request.shaderName, includeDirectoryCount, defineCount, argumentReserve, ownedArgumentCount))
        return false;

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
    arguments.push_back(slangStage);
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

