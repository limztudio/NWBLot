// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "slang_compiler.h"

#include "arena_names.h"
#include "binary_payload.h"

#include <impl/assets_material/shader_stage_names.h>

#include <core/assets/paths.h>
#include <core/common/log.h>
#include <global/hash_utils.h>
#include <global/process_execution.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Assets = Core::Assets;
namespace Alloc = Core::Alloc;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_SLANGC_EXECUTABLE
#error "NWB_SLANGC_EXECUTABLE must be defined by the build configuration"
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_slang_compiler{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CookString = ShaderCook::CookString;
template<typename T>
using CookVector = ShaderCook::CookVector<T>;
template<typename T, typename V>
using CookMap = ShaderCook::CookMap<T, V>;
template<typename T>
using CookHashSet = ShaderCook::CookHashSet<T>;
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


static constexpr AStringView s_SlangIncludeDirective = "include";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool IsIncludeDirectiveBoundary(const AStringView line, const usize cursor){
    return cursor >= line.size() || IsAsciiSpace(line[cursor]) || line[cursor] == '"' || line[cursor] == '<';
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr usize s_BaseCompilerArgumentCount = 16u + LengthOf(s_SpirvBaselineCapabilities) * 2u;
static constexpr usize s_MaxTargetProfileCapabilityCount = 1u;
static constexpr usize s_MaxOptimizationArgumentCount = 1u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename StringT>
static bool ReadDiagnostics(const Path& diagnosticsPath, StringT& outDiagnostics){
    outDiagnostics.clear();

    ErrorCode errorCode;
    if(!FileExists(diagnosticsPath, errorCode) || errorCode)
        return false;

    return ReadTextFile(diagnosticsPath, outDiagnostics);
}
static void RemoveFileBestEffort(const Path& path){
    ErrorCode errorCode;
    const bool exists = FileExists(path, errorCode);
    if(errorCode){
        if(!IsMissingPathError(errorCode)){
            NWB_LOGGER_WARNING(GLB_TEXT("ShaderCook: failed to query stale compiler output '{}' before cleanup: {}")
                , PathToString<tchar>(path)
                , StringConvert(errorCode.message())
            );
        }
        return;
    }

    if(exists){
        errorCode.clear();
        if(!RemoveFile(path, errorCode)){
            if(errorCode){
                NWB_LOGGER_WARNING(GLB_TEXT("ShaderCook: failed to remove stale compiler output '{}': {}")
                    , PathToString<tchar>(path)
                    , StringConvert(errorCode.message())
                );
            }
            else{
                NWB_LOGGER_WARNING(GLB_TEXT("ShaderCook: failed to remove stale compiler output '{}'")
                    , PathToString<tchar>(path)
                );
            }
        }
    }
}
class ScopedFileCleanupGuard final : NoCopy{
public:
    explicit ScopedFileCleanupGuard(const Path& path)noexcept
        : m_path(path)
    {}
    ~ScopedFileCleanupGuard(){
        if(m_active)
            RemoveFileBestEffort(m_path);
    }


public:
    void dismiss()noexcept{ m_active = false; }


private:
    const Path& m_path;
    bool m_active = true;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ScopedDirectoryCleanupGuard final : NoCopy{
public:
    explicit ScopedDirectoryCleanupGuard(const Path& path)noexcept
        : m_path(path)
    {}
    ~ScopedDirectoryCleanupGuard(){
        if(!m_active)
            return;

        ErrorCode errorCode;
        if(RemoveAllIfExists(m_path, errorCode))
            return;

        if(errorCode){
            NWB_LOGGER_WARNING(GLB_TEXT("ShaderCook: failed to remove temporary compiler source directory '{}' : {}")
                , PathToString<tchar>(m_path)
                , StringConvert(errorCode.message())
            );
        }
        else{
            NWB_LOGGER_WARNING(GLB_TEXT("ShaderCook: failed to remove temporary compiler source directory '{}'"), PathToString<tchar>(m_path));
        }
    }


public:
    void dismiss()noexcept{ m_active = false; }


private:
    const Path& m_path;
    bool m_active = true;
};


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

    outPath = overlayRoot / relativePath;
    return !outPath.empty();
}


static bool IsCompilerDependency(const ShaderCook::CookVector<Path>& dependencies, const Path& absolutePath){
    for(const Path& dependency : dependencies){
        if(dependency == absolutePath)
            return true;
    }
    return false;
}


static void RewriteAbsoluteCompilerIncludes(
    ScratchString& inOutSource,
    const Path& sourcePath,
    const ShaderCook::ShaderCompilerRequest& request,
    const Path& overlayRoot,
    Alloc::ScratchArena& scratchArena
){
    ScratchString rewrittenSource{scratchArena};
    rewrittenSource.reserve(inOutSource.size());

    const AStringView sourceView(inOutSource.data(), inOutSource.size());
    usize lineBegin = 0u;
    while(lineBegin < sourceView.size()){
        usize lineEnd = lineBegin;
        while(lineEnd < sourceView.size() && sourceView[lineEnd] != '\n')
            ++lineEnd;

        const AStringView line = sourceView.substr(lineBegin, lineEnd - lineBegin);
        AStringView includeName;
        ShaderIncludeKind::Enum includeKind;
        if(!SlangShaderCompiler::ExtractIncludeDirective(line, includeName, includeKind)){
            rewrittenSource.append(line.data(), line.size());
        }
        else{
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
                    rewrittenSource.append(line.data(), line.size());
                }
                else{
                    const usize includeNameOffset = line.find(includeName);
                    if(includeNameOffset == AStringView::npos){
                        rewrittenSource.append(line.data(), line.size());
                    }
                    else{
                        const ScratchString overlayIncludeText = PathToString<char>(scratchArena, overlayIncludePath);
                        rewrittenSource.append(line.data(), includeNameOffset);
                        rewrittenSource += overlayIncludeText;
                        rewrittenSource.append(
                            line.data() + includeNameOffset + includeName.size(),
                            line.size() - includeNameOffset - includeName.size()
                        );
                    }
                }
            }
        }

        if(lineEnd < sourceView.size())
            rewrittenSource += '\n';
        lineBegin = lineEnd + 1u;
    }

    inOutSource = Move(rewrittenSource);
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
        NWB_LOGGER_ERROR(GLB_TEXT("ShaderCook: compiler request for '{}' has no resolved source dependencies"), StringConvert(request.shaderName));
        return false;
    }

    ScratchString sourceText{scratchArena};
    bool hasBom = false;
    for(const Path& dependency : request.dependencies){
        sourceText.clear();
        if(!ReadTextFile(dependency, sourceText)){
            NWB_LOGGER_ERROR(GLB_TEXT("ShaderCook: failed to read compiler input '{}' while normalizing UTF-8 BOM")
                , PathToString<tchar>(dependency)
            );
            return false;
        }

        const usize sourceSize = sourceText.size();
        StripUtf8Bom(sourceText);
        hasBom = hasBom || sourceText.size() != sourceSize;
    }

    if(!hasBom){
        outIncludeDirectories.reserve(request.includeDirectories.size());
        for(const Path& includeDirectory : request.includeDirectories)
            outIncludeDirectories.push_back(includeDirectory);
        return true;
    }

    ErrorCode errorCode;
    if(!EnsureEmptyDirectory(overlayRoot, errorCode)){
        NWB_LOGGER_ERROR(GLB_TEXT("ShaderCook: failed to create temporary compiler source directory '{}' : {}")
            , PathToString<tchar>(overlayRoot)
            , StringConvert(errorCode.message())
        );
        return false;
    }

    // Preserve the original absolute hierarchy so nested relative includes still resolve inside the BOM-stripped overlay.
    for(const Path& dependency : request.dependencies){
        Path overlayDependencyPath(dependency.arena());
        if(!BuildCompilerOverlayPath(overlayRoot, dependency, overlayDependencyPath)){
            NWB_LOGGER_ERROR(GLB_TEXT("ShaderCook: failed to map compiler input '{}' into the temporary source directory")
                , PathToString<tchar>(dependency)
            );
            return false;
        }

        errorCode.clear();
        if(!EnsureDirectories(overlayDependencyPath.parentPath(), errorCode)){
            NWB_LOGGER_ERROR(GLB_TEXT("ShaderCook: failed to create temporary compiler source parent '{}' : {}")
                , PathToString<tchar>(overlayDependencyPath.parentPath())
                , StringConvert(errorCode.message())
            );
            return false;
        }

        sourceText.clear();
        if(!ReadTextFile(dependency, sourceText)){
            NWB_LOGGER_ERROR(GLB_TEXT("ShaderCook: failed to read compiler input '{}' while writing BOM-stripped source")
                , PathToString<tchar>(dependency)
            );
            return false;
        }
        StripUtf8Bom(sourceText);
        RewriteAbsoluteCompilerIncludes(sourceText, dependency, request, overlayRoot, scratchArena);
        if(!WriteTextFile(overlayDependencyPath, AStringView(sourceText.data(), sourceText.size()))){
            NWB_LOGGER_ERROR(GLB_TEXT("ShaderCook: failed to write BOM-stripped compiler input '{}'"), PathToString<tchar>(overlayDependencyPath));
            return false;
        }
    }

    ErrorCode sourcePathError;
    const Path absoluteSourcePath = AbsolutePath(request.sourcePath, sourcePathError).lexicallyNormal();
    if(sourcePathError || !IsCompilerDependency(request.dependencies, absoluteSourcePath) || !BuildCompilerOverlayPath(overlayRoot, absoluteSourcePath, outSourcePath)){
        NWB_LOGGER_ERROR(GLB_TEXT("ShaderCook: failed to map source '{}' into the temporary compiler source directory")
            , PathToString<tchar>(request.sourcePath)
        );
        return false;
    }

    if(request.includeDirectories.size() > Limit<usize>::s_Max / 2u){
        NWB_LOGGER_ERROR(GLB_TEXT("ShaderCook: compiler request for '{}' has too many include directories"), StringConvert(request.shaderName));
        return false;
    }

    outIncludeDirectories.reserve(request.includeDirectories.size() * 2u);
    for(const Path& includeDirectory : request.includeDirectories){
        errorCode.clear();
        const Path absoluteIncludeDirectory = AbsolutePath(includeDirectory, errorCode).lexicallyNormal();
        Path overlayIncludeDirectory(includeDirectory.arena());
        if(errorCode || !BuildCompilerOverlayPath(overlayRoot, absoluteIncludeDirectory, overlayIncludeDirectory)){
            NWB_LOGGER_ERROR(GLB_TEXT("ShaderCook: failed to map include directory '{}' into the temporary compiler source directory")
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


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool SlangShaderCompiler::ExtractIncludeDirective(const AStringView line, AStringView& outIncludeName, ShaderIncludeKind::Enum& outKind){
    outIncludeName = {};
    outKind = ShaderIncludeKind::Relative;

    usize cursor = 0;
    while(cursor < line.size() && IsAsciiSpace(line[cursor]))
        ++cursor;

    if(cursor >= line.size() || line[cursor] != '#')
        return false;

    ++cursor;
    while(cursor < line.size() && IsAsciiSpace(line[cursor]))
        ++cursor;

    if(line.substr(cursor, __hidden_slang_compiler::s_SlangIncludeDirective.size()) != __hidden_slang_compiler::s_SlangIncludeDirective)
        return false;
    cursor += __hidden_slang_compiler::s_SlangIncludeDirective.size();
    if(!__hidden_slang_compiler::IsIncludeDirectiveBoundary(line, cursor))
        return false;

    while(cursor < line.size() && IsAsciiSpace(line[cursor]))
        ++cursor;

    if(cursor >= line.size())
        return false;

    char closingDelimiter = '"';
    if(line[cursor] == '"'){
        outKind = ShaderIncludeKind::Relative;
        closingDelimiter = '"';
    }
    else if(line[cursor] == '<'){
        outKind = ShaderIncludeKind::Standard;
        closingDelimiter = '>';
    }
    else{
        return false;
    }
    ++cursor;

    const usize closingDelimiterPos = line.find(closingDelimiter, cursor);
    if(closingDelimiterPos == AStringView::npos || closingDelimiterPos <= cursor)
        return false;

    outIncludeName = line.substr(cursor, closingDelimiterPos - cursor);
    return !outIncludeName.empty();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool SlangShaderCompiler::ResolveIncludeFile(const AStringView includeName, const ShaderIncludeKind::Enum kind, const Path& sourceDirectory, const ShaderCook::CookVector<Path>& includeDirectories, Path& outPath){
    ErrorCode errorCode;

    if(kind == ShaderIncludeKind::Relative){
        const Path localCandidate = (sourceDirectory / includeName).lexicallyNormal();
        errorCode.clear();
        if(IsRegularFile(localCandidate, errorCode)){
            outPath = localCandidate;
            return true;
        }
        if(errorCode && !IsMissingPathError(errorCode)){
            NWB_LOGGER_ERROR(GLB_TEXT("Failed to query include candidate '{}': {}")
                , PathToString<tchar>(localCandidate)
                , StringConvert(errorCode.message())
            );
            return false;
        }
    }

    for(const Path& includeDirectory : includeDirectories){
        const Path includeCandidate = (includeDirectory / includeName).lexicallyNormal();
        errorCode.clear();
        if(IsRegularFile(includeCandidate, errorCode)){
            outPath = includeCandidate;
            return true;
        }
        if(errorCode && !IsMissingPathError(errorCode)){
            NWB_LOGGER_ERROR(GLB_TEXT("Failed to query include candidate '{}': {}")
                , PathToString<tchar>(includeCandidate)
                , StringConvert(errorCode.message())
            );
            return false;
        }
    }

    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool SlangShaderCompiler::compileVariant(const ShaderCook::ShaderCompilerRequest& request, ShaderCook::CookVector<u8>& outBytecode){
        outBytecode.clear();

        if(request.sourcePath.empty()){
            NWB_LOGGER_ERROR(GLB_TEXT("Failed to compile shader '{}' : source path is empty"), StringConvert(request.shaderName));
            return false;
        }

        if(request.outputPath.empty()){
            NWB_LOGGER_ERROR(GLB_TEXT("Failed to compile shader '{}' : output path is empty"), StringConvert(request.shaderName));
            return false;
        }

        if(request.entryPoint.empty()){
            NWB_LOGGER_ERROR(GLB_TEXT("Failed to compile shader '{}' : entry point is empty"), StringConvert(request.shaderName));
            return false;
        }

        AStringView slangStage;
        if(!TryMapStageToSlangStage(request.stage, slangStage)){
            NWB_LOGGER_ERROR(GLB_TEXT("Unknown shader stage '{}' in entry '{}'"), StringConvert(request.stage), StringConvert(request.shaderName));
            return false;
        }

        const AStringView optimizationArgument = SlangOptimizationArgument(request.optimizationLevel);
        if(request.optimizationLevel >= ShaderOptimizationLevel::kCount){
            NWB_LOGGER_ERROR(GLB_TEXT("Shader '{}' uses an invalid optimization level {}")
                , StringConvert(request.shaderName)
                , static_cast<u32>(request.optimizationLevel)
            );
            return false;
        }

        Path diagnosticsPath = request.outputPath;
        diagnosticsPath += ".diag";
        __hidden_slang_compiler::RemoveFileBestEffort(request.outputPath);
        __hidden_slang_compiler::RemoveFileBestEffort(diagnosticsPath);
        __hidden_slang_compiler::ScopedFileCleanupGuard outputCleanup(request.outputPath);
        __hidden_slang_compiler::ScopedFileCleanupGuard diagnosticsCleanup(diagnosticsPath);

        Alloc::ScratchArena argumentArena(AssetsShaderArenaScope::s_CompilerArgumentsArena);
        Path compilerOverlayRoot = request.outputPath;
        compilerOverlayRoot += ".bom_sources";
        __hidden_slang_compiler::ScopedDirectoryCleanupGuard compilerOverlayCleanup(compilerOverlayRoot);
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
            NWB_LOGGER_ERROR(GLB_TEXT("ShaderCook: compiler request for '{}' has too many arguments"), StringConvert(request.shaderName));
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
        arguments.push_back("-target");
        arguments.push_back("spirv");
        arguments.push_back("-emit-spirv-directly");
        // The runtime pipeline selects the authored entry-point identifier, so retain its exact spelling in SPIR-V.
        arguments.push_back("-fvk-use-entrypoint-name");
        arguments.push_back("-warnings-as-errors");
        arguments.push_back("all");
        if(!optimizationArgument.empty())
            arguments.push_back(optimizationArgument);
        arguments.push_back("-profile");
        arguments.push_back(MaterialShaderStageNames::s_Spirv15TargetProfileText);
        if(request.rayQuery){
            arguments.push_back("-capability");
            arguments.push_back(MaterialShaderStageNames::s_SpvRayQueryCapabilityText);
        }
        for(const AStringView capability : __hidden_slang_compiler::s_SpirvBaselineCapabilities){
            arguments.push_back("-capability");
            arguments.push_back(capability);
        }
        arguments.push_back("-entry");
        arguments.push_back(request.entryPoint);
        arguments.push_back("-stage");
        arguments.push_back(slangStage);
        for(const Path& includeDirectory : compilerIncludeDirectories){
            arguments.push_back("-I");
            ownedArguments.push_back(PathToString<char>(argumentArena, includeDirectory));
            arguments.push_back(AStringView(ownedArguments.back()));
        }

        for(u32 i = 0u; i < request.defineCount; ++i){
            const ShaderCook::ShaderMacroDefinition& define = request.defines[i];
            ownedArguments.emplace_back("-D", argumentArena);
            __hidden_slang_compiler::ScratchString& defineArgument = ownedArguments.back();
            defineArgument += define.name;
            if(!define.value.empty()){
                defineArgument += '=';
                defineArgument += define.value;
            }
            arguments.push_back(AStringView(defineArgument));
        }

        arguments.push_back("-o");
        ownedArguments.push_back(PathToString<char>(argumentArena, request.outputPath));
        arguments.push_back(AStringView(ownedArguments.back()));

        const __hidden_slang_compiler::ScratchString diagnosticsPathText = PathToString<char>(argumentArena, diagnosticsPath);
        bool exitCodeQueryFailed = false;
        const int exitCode = ::RunProcessRedirectedToFile(
            argumentArena,
            arguments,
            AStringView(diagnosticsPathText),
            &exitCodeQueryFailed
        );
        if(exitCodeQueryFailed)
            NWB_LOGGER_WARNING(GLB_TEXT("ShaderCook: failed to query compiler process exit code"));
        if(exitCode != 0){
            __hidden_slang_compiler::ScratchString diagnostics{argumentArena};
            if(__hidden_slang_compiler::ReadDiagnostics(diagnosticsPath, diagnostics) && !diagnostics.empty()){
                NWB_LOGGER_ERROR(GLB_TEXT("Shader compile failed for '{}' (variant '{}') :\n{}")
                    , StringConvert(request.shaderName)
                    , StringConvert(request.variantName)
                    , StringConvert(diagnostics)
                );
            }
            else{
                NWB_LOGGER_ERROR(GLB_TEXT("Shader compile failed for '{}' (variant '{}') with exit code {}")
                    , StringConvert(request.shaderName)
                    , StringConvert(request.variantName)
                    , exitCode
                );
            }
            return false;
        }

        __hidden_slang_compiler::ScratchString diagnostics{argumentArena};
        if(__hidden_slang_compiler::ReadDiagnostics(diagnosticsPath, diagnostics) && !diagnostics.empty()){
            NWB_LOGGER_ERROR(GLB_TEXT("Shader compiler emitted unexpected diagnostics for '{}' (variant '{}') :\n{}")
                , StringConvert(request.shaderName)
                , StringConvert(request.variantName)
                , StringConvert(diagnostics)
            );
            return false;
        }

        ErrorCode errorCode;
        if(!ReadBinaryFile(request.outputPath, outBytecode, errorCode)){
            if(errorCode){
                NWB_LOGGER_ERROR(GLB_TEXT("Shader compile failed for '{}' (variant '{}') : failed to read output '{}' : {}")
                    , StringConvert(request.shaderName)
                    , StringConvert(request.variantName)
                    , PathToString<tchar>(request.outputPath)
                    , StringConvert(errorCode.message())
                );
            }
            else{
                NWB_LOGGER_ERROR(GLB_TEXT("Shader compile failed for '{}' (variant '{}') : failed to read output '{}'")
                    , StringConvert(request.shaderName)
                    , StringConvert(request.variantName)
                    , PathToString<tchar>(request.outputPath)
                );
            }
            return false;
        }

        switch(ShaderBinaryPayload::ValidateBytecode(outBytecode)){
        case ShaderBinaryPayload::BytecodeValidationFailure::None:
            break;
        case ShaderBinaryPayload::BytecodeValidationFailure::InvalidSize:
            NWB_LOGGER_ERROR(GLB_TEXT("Shader compile failed for '{}' (variant '{}') : compiled bytecode has invalid size {}")
                , StringConvert(request.shaderName)
                , StringConvert(request.variantName)
                , outBytecode.size()
            );
            outBytecode.clear();
            return false;
        case ShaderBinaryPayload::BytecodeValidationFailure::InvalidMagic:
            NWB_LOGGER_ERROR(GLB_TEXT("Shader compile failed for '{}' (variant '{}') : compiled bytecode has invalid SPIR-V magic")
                , StringConvert(request.shaderName)
                , StringConvert(request.variantName)
            );
            outBytecode.clear();
            return false;
        }

        outputCleanup.dismiss();
        return true;
    }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool SlangShaderCompiler::TryMapStageToSlangStage(const AStringView stage, AStringView& outStage){
    struct SlangStageMapping{
        AStringView name;
        AStringView slangStage;
    };

    static constexpr SlangStageMapping s_StageMappings[] = {
        { MaterialShaderStageNames::s_VertexArchiveStageText, "vertex" },
        { MaterialShaderStageNames::s_PixelArchiveStageText, "fragment" },
        { MaterialShaderStageNames::s_ComputeArchiveStageText, "compute" },
        { MaterialShaderStageNames::s_MeshArchiveStageText, "mesh" },
        { MaterialShaderStageNames::s_RayGenerationArchiveStageText, "raygeneration" },
        { MaterialShaderStageNames::s_RayMissArchiveStageText, "miss" },
        { MaterialShaderStageNames::s_RayClosestHitArchiveStageText, "closesthit" },
        { MaterialShaderStageNames::s_RayAnyHitArchiveStageText, "anyhit" },
        { MaterialShaderStageNames::s_RayIntersectionArchiveStageText, "intersection" },
        { MaterialShaderStageNames::s_RayCallableArchiveStageText, "callable" },
    };

    for(const SlangStageMapping& mapping : s_StageMappings){
        if(stage == mapping.name){
            outStage = mapping.slangStage;
            return true;
        }
    }

    outStage = {};
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AStringView SlangShaderCompiler::SlangOptimizationArgument(const ShaderOptimizationLevel::Enum optimizationLevel){
    switch(optimizationLevel){
    case ShaderOptimizationLevel::None: return "-O0";
    case ShaderOptimizationLevel::Default: return {};
    case ShaderOptimizationLevel::High: return "-O2";
    case ShaderOptimizationLevel::Maximal: return "-O3";
    default: return {};
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Core::GlobalUniquePtr<ShaderCook::IShaderCompiler> CreateSlangShaderCompiler(ShaderCook::CookArena& memoryArena){
    return Core::MakeGlobalUnique<SlangShaderCompiler>(memoryArena, memoryArena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

