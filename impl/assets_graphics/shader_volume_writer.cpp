// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shader_cook_plan.h"
#include <impl/assets_shader/shader_define_planner.h>
#include "shader_variant_bytecode_cache.h"

#include "csg_shader_variants.h"

#include <impl/assets_shader/asset.h>
#include <impl/assets_shader/slang_compiler.h>

#include <core/graphics/shader_archive.h>
#include <core/graphics/shader_stage_names.h>
#include <core/graphics/spirv_entry_point.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_shader_volume_writer{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace AssetsGraphicsCookDetail;
using Core::Assets::CookString;
using Core::Assets::ScratchArena;
using Core::Assets::ScratchString;
using Core::Assets::CookEntryPathHashSet;

using ScratchPath = ::Path<ScratchArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<usize> ReserveShaderIndexRecords(const Span<const PreparedShaderEntry> preparedEntries, Vector<Core::ShaderArchive::Record, ScratchArena>& outShaderIndexRecords){
    u64 shaderRecordCount = 0;
    for(const PreparedShaderEntry& preparedEntry : preparedEntries){
        if(!TryAccumulatePreparedShaderRecordCount(preparedEntry.variantCount, shaderRecordCount))
            return MakeUnexpected(Failure{});
    }
    usize count = 0u;
    if(!ValidatePreparedShaderRecordCountFitsCapacity(shaderRecordCount, count))
        return MakeUnexpected(Failure{});

    outShaderIndexRecords.clear();
    outShaderIndexRecords.reserve(count);
    return count;
}

static b8 AppendShaderIndexToManifest(
    const Span<const Core::ShaderArchive::Record> shaderIndexRecords,
    ShaderCook::CookArena& cookArena,
    ScratchArena& scratchArena,
    Core::Assets::AssetsVolumeCookDetail::AssetVolumePackManifest& inOutManifest,
    CookEntryPathHashSet& inOutSeenVirtualPathHashes
){
    const Name& shaderIndexVirtualPath = Core::ShaderArchive::s_IndexVirtualPathName;
    if(!inOutSeenVirtualPathHashes.insert(shaderIndexVirtualPath.hash()).second){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: duplicate shader archive index virtual path '{}'"), ConvertString(shaderIndexVirtualPath.resolvedText()));
        return false;
    }

    const auto indexBinary = Core::ShaderArchive::SerializeIndex(shaderIndexRecords, cookArena, scratchArena);
    if(!indexBinary){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: failed to serialize shader index"));
        return false;
    }
    if(Core::Assets::AssetsVolumeCookDetail::AppendPayloadBytesToManifest(shaderIndexVirtualPath, *indexBinary, inOutManifest))
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: failed to append shader index to manifest"));
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsGraphicsCookDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 AppendPreparedShadersToManifest(
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const Span<const PreparedShaderEntry> preparedEntries,
    ShaderCook::CookArena& cookArena,
    Core::Assets::ScratchArena& scratchArena,
    Core::Assets::AssetsVolumeCookDetail::AssetVolumePackManifest& manifest,
    Core::Assets::CookEntryPathHashSet& inOutSeenVirtualPathHashes
){
    Vector<Core::ShaderArchive::Record, Core::Assets::ScratchArena> shaderIndexRecords{scratchArena};
    const auto shaderRecordCount = __hidden_shader_volume_writer::ReserveShaderIndexRecords(preparedEntries, shaderIndexRecords);
    if(!shaderRecordCount)
        return false;

    u64 compilerFingerprint = 0u;
    if(!preparedEntries.empty()){
        const auto fingerprint = SlangShaderCompiler::ComputeCompilerFingerprint(cacheDirectory, scratchArena);
        if(!fingerprint)
            return false;
        compilerFingerprint = *fingerprint;
    }

    Core::GraphicsBytes cookedBytecode{cookArena};
    Core::GraphicsBytes shaderAssetPayload{cookArena};
    ShaderCook::CookVector<ShaderCook::DefineCombo> defineCombinations{ cookArena };
    shaderIndexRecords.clear();

    for(const PreparedShaderEntry& preparedEntry : preparedEntries){
        const ShaderCook::ShaderEntry& entry = preparedEntry.entry;
        const auto shaderNameResult = ToName(entry.name);
        const auto stageNameResult = ToName(entry.archiveStage);
        if(!shaderNameResult || !stageNameResult || !*shaderNameResult || !*stageNameResult || entry.entryPoint.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Shader cook failed to canonicalize shader identity for '{}' stage '{}' entry point '{}'"), ConvertString(entry.name), ConvertString(entry.archiveStage), ConvertString(entry.entryPoint));
            return false;
        }
        const Name shaderName = **shaderNameResult;
        const Name stageName = **stageNameResult;

        auto appendShaderVariant = [&](const ShaderCook::DefineCombo& defineCombo) -> b8{
            const Core::Assets::CookString generatedVariantName = ShaderCook::BuildCanonicalVariantName(defineCombo, cookArena, scratchArena);

            const u64 sourceChecksum = ShaderCook::ComputeSourceChecksum(entry, generatedVariantName, preparedEntry.dependencyChecksum, compilerFingerprint, scratchArena);
            const Core::Assets::ScratchString sourceChecksumHex = FormatHex64A(scratchArena, sourceChecksum);

            const __hidden_shader_volume_writer::ScratchPath bytecodeCachePath = ShaderVariantBytecodeCache::buildPath(cacheDirectory, configurationSafeName, sourceChecksumHex, scratchArena);
            if(!ShaderVariantBytecodeCache::getBytecode(
                entry,
                generatedVariantName,
                defineCombo,
                preparedEntry.includeDirectories,
                preparedEntry.dependencies,
                preparedEntry.sourcePath,
                bytecodeCachePath,
                preparedEntry.compilerInputsHaveBom,
                scratchArena,
                cookedBytecode
            ))
                return false;

            const Expected<Name> virtualPathResult = Core::ShaderArchive::BuildVirtualPathName(shaderName, generatedVariantName, stageName);
            if(!virtualPathResult){
                NWB_LOGGER_ERROR(NWB_TEXT("Shader cook failed to build virtual path for '{}' stage '{}' variant '{}'"), ConvertString(entry.name), ConvertString(entry.archiveStage), ConvertString(generatedVariantName));
                return false;
            }
            const Name& virtualPath = *virtualPathResult;

            const NameHash virtualPathHash = virtualPath.hash();
            if(!inOutSeenVirtualPathHashes.insert(virtualPathHash).second){
                NWB_LOGGER_ERROR(NWB_TEXT("Shader cook produced duplicate virtual path '{}' (entry='{}', variant='{}')"), ConvertString(virtualPath.resolvedText()), ConvertString(entry.name), ConvertString(generatedVariantName));
                return false;
            }

            const u64 bytecodeChecksum = ComputeFnv64Bytes(cookedBytecode.data(), cookedBytecode.size());
            const u64 cookKeyHash = BuildShaderVariantCookKeyHash(virtualPathHash, sourceChecksum, bytecodeChecksum);
            const ShaderBinaryPayload::AssetPayloadEncodeFailure::Enum payloadFailure = ShaderBinaryPayload::EncodeAssetPayload(AStringView(entry.entryPoint), cookedBytecode, shaderAssetPayload);
            if(payloadFailure != ShaderBinaryPayload::AssetPayloadEncodeFailure::None){
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to package shader payload '{}' (failure {})"), ConvertString(virtualPath.resolvedText()), static_cast<u32>(payloadFailure));
                return false;
            }
            if(!Core::Assets::AssetsVolumeCookDetail::AppendPayloadBytesToManifest(virtualPath, shaderAssetPayload, manifest, cookKeyHash)){
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to append shader payload '{}' to manifest"), ConvertString(virtualPath.resolvedText()));
                return false;
            }

            Core::ShaderArchive::Record record(cookArena);
            record.shaderName = shaderName;
            record.variantName = generatedVariantName;
            record.stage = stageName;
            record.sourceChecksum = sourceChecksum;
            record.bytecodeChecksum = bytecodeChecksum;
            record.virtualPathHash = virtualPathHash;
            NWB_ASSERT(shaderIndexRecords.size() < *shaderRecordCount);
            shaderIndexRecords.push_back(Move(record));
            return true;
        };

        if(!ShaderDefinePlanner(cookArena).expandDefineCombinations(entry.defineValues, scratchArena, defineCombinations)){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: variant combination count exceeds runtime limits for entry '{}'"), ConvertString(entry.name));
            return false;
        }
        if(defineCombinations.empty())
            defineCombinations.push_back(ShaderCook::DefineCombo(cookArena));

        for(const ShaderCook::DefineCombo& defineCombo : defineCombinations){
            if(!appendShaderVariant(defineCombo))
                return false;

            if(preparedEntry.supportsCsgClipVariant || preparedEntry.supportsAvboitCsgClipVariant){
                const auto csgDefineCombo = AssetsGraphicsCsgShaderVariants::BuildClipDefineCombo(AStringView(entry.name), defineCombo, cookArena);
                if(!csgDefineCombo)
                    return false;

                if(!appendShaderVariant(*csgDefineCombo))
                    return false;
            }
        }
    }

    NWB_ASSERT(shaderIndexRecords.size() == *shaderRecordCount);
    return __hidden_shader_volume_writer::AppendShaderIndexToManifest(shaderIndexRecords, cookArena, scratchArena, manifest, inOutSeenVirtualPathHashes);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

