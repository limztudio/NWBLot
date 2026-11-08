// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


#include "shader_variant_bytecode_cache.h"
#include "csg_shader_variants.h"

#include <impl/assets_shader/asset.h>
#include <impl/assets_shader/slang_compiler.h>

#include <core/graphics/shader_stage_names.h>
#include <core/graphics/spirv_entry_point.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using Core::Assets::ScratchArena;
using Core::Assets::ScratchString;


ShaderVariantBytecodeCache::ScratchPath ShaderVariantBytecodeCache::BuildPath(const Path& cacheDirectory, const AStringView configurationSafeName, const AStringView sourceChecksumHex, ScratchArena& scratchArena){
    ScratchString bytecodeFileName(sourceChecksumHex, scratchArena);
    bytecodeFileName += s_ShaderVariantBytecodeExtension;
    return ScratchPath(scratchArena, cacheDirectory) / configurationSafeName / bytecodeFileName;
}

ShaderVariantCacheReadStatus::Enum ShaderVariantBytecodeCache::ReadCached(const ShaderCook::ShaderEntry& entry, const ScratchPath& bytecodePath, Core::GraphicsBytes& outBytecode){
    const auto read = ReadBinaryFile(bytecodePath, outBytecode);
    if(read){
        const auto stageName = ToName(entry.stage);
        if(!stageName){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: shader stage contains an embedded null for entry '{}'"), ConvertString(entry.name));
            return ShaderVariantCacheReadStatus::Error;
        }
        const Core::ShaderType::Enum shaderType = Core::ShaderStageNames::ShaderTypeFromArchiveStageName(*stageName ? **stageName : s_NameNone);
        if(Core::ResolveSpirvEntryPointName(BinaryByteView{ outBytecode.data(), outBytecode.size() }, AStringView(entry.entryPoint), Core::ShaderType::ToMask(shaderType)))
            return ShaderVariantCacheReadStatus::Hit;

        outBytecode.clear();
        return ShaderVariantCacheReadStatus::Miss;
    }

    if(!IsMissingPathError(read.error())){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: failed to read bytecode cache '{}' for entry '{}': {}"), PathToString<tchar>(bytecodePath), ConvertString(entry.name), ConvertString(read.error().message()));
        return ShaderVariantCacheReadStatus::Error;
    }

    outBytecode.clear();
    return ShaderVariantCacheReadStatus::Miss;
}

b8 ShaderVariantBytecodeCache::GetBytecode(
    const ShaderCook::ShaderEntry& entry,
    const AStringView variantName,
    const ShaderCook::DefineCombo& defineCombo,
    const Span<const Path> includeDirectories,
    const Span<const Path> dependencies,
    const Path& sourcePath,
    const ScratchPath& bytecodeCachePath,
    const b8 compilerInputsHaveBom,
    ScratchArena& scratchArena,
    Core::GraphicsBytes& outBytecode
){

    outBytecode.clear();

    const ShaderVariantCacheReadStatus::Enum bytecodeCacheStatus = ReadCached(entry, bytecodeCachePath, outBytecode);
    if(bytecodeCacheStatus == ShaderVariantCacheReadStatus::Error)
        return false;
    if(bytecodeCacheStatus == ShaderVariantCacheReadStatus::Hit)
        return true;

    HashMap<AStringView, AStringView, ScratchArena, Hasher<AStringView>, EqualTo<AStringView>> mergedDefines(0, Hasher<AStringView>(), EqualTo<AStringView>(), scratchArena);
    // Two live container sizes cannot overflow usize.
    mergedDefines.reserve(defineCombo.size() + entry.implicitDefines.size());
    for(const auto& [defineName, value] : defineCombo)
        mergedDefines.insert_or_assign(defineName, value);
    for(const auto& [defineName, value] : entry.implicitDefines)
        mergedDefines.insert_or_assign(AStringView(defineName), AStringView(value));

    Vector<ShaderBinaryPayload::ShaderMacroDefinition, ScratchArena> compileDefines{ scratchArena };
    if(!ValidateShaderMergedDefineCount(entry.name, mergedDefines.size()))
        return false;
    compileDefines.reserve(mergedDefines.size());
    for(const auto& [defineName, value] : mergedDefines)
        compileDefines.push_back(ShaderBinaryPayload::ShaderMacroDefinition{ defineName, value });
    Sort(compileDefines.begin(), compileDefines.end(), [](const ShaderBinaryPayload::ShaderMacroDefinition& lhs, const ShaderBinaryPayload::ShaderMacroDefinition& rhs)noexcept{
        return lhs.name < rhs.name;
    });

    const ScratchPath cacheParentPath = bytecodeCachePath.parentPath();
    auto ensureDirectoriesResult = EnsureDirectories(cacheParentPath);
    if(!ensureDirectoriesResult){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: failed to create cache directory '{}': {}"), PathToString<tchar>(cacheParentPath), ConvertString(ensureDirectoriesResult.error().message()));
        return false;
    }

    const ShaderCook::ShaderCompilerRequest compileRequest = {
        .shaderName = entry.name,
        .stage = entry.stage,
        .entryPoint = entry.entryPoint,
        .variantName = variantName,
        .defines = compileDefines,
        .includeDirectories = includeDirectories,
        .dependencies = dependencies,
        .externallyPlannedMacroIncludes = AssetsGraphicsCsgShaderVariants::s_ExternallyPlannedMacroIncludes,
        .sourcePath = sourcePath,
        .outputPath = bytecodeCachePath,
        .optimizationLevel = entry.optimizationLevel,
        .rayQuery = entry.rayQuery,
        .compilerInputsHaveBom = compilerInputsHaveBom,
    };
    return SlangShaderCompiler::CompileVariant(compileRequest, scratchArena, outBytecode);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

