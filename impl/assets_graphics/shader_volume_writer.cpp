// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shader_volume_writer.h"

#include "csg_shader_variants.h"

#include <impl/assets_shader/binary_payload.h>
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

namespace CacheReadStatus{
    enum Enum : u8{
        Hit = 0,
        Miss,
        Error
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Path BuildVariantCachePath(
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const AStringView sourceChecksumHex,
    ScratchArena& scratchArena
){
    ScratchString bytecodeFileName(sourceChecksumHex, scratchArena);
    bytecodeFileName += ".spv";
    return cacheDirectory / configurationSafeName / bytecodeFileName;
}

static CacheReadStatus::Enum TryReadCachedBytecode(
    const ShaderCook::ShaderEntry& entry,
    const Path& bytecodePath,
    Core::GraphicsBytes& outBytecode
){
    ErrorCode errorCode;
    if(ReadBinaryFile(bytecodePath, outBytecode, errorCode)){
        const Core::ShaderType::Enum shaderType = Core::ShaderStageNames::ShaderTypeFromArchiveStageName(ToName(entry.stage.view()));
        AStringView validatedEntryPoint;
        if(Core::ResolveSpirvEntryPointName(
            BinaryByteView{ outBytecode.data(), outBytecode.size() },
            AStringView(entry.entryPoint),
            Core::ShaderType::ToMask(shaderType),
            validatedEntryPoint
        ) == Core::SpirvEntryPointLookupResult::Found)
            return CacheReadStatus::Hit;

        outBytecode.clear();
        return CacheReadStatus::Miss;
    }

    if(errorCode && !IsMissingPathError(errorCode)){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: failed to read bytecode cache '{}' for entry '{}': {}")
            , PathToString<tchar>(bytecodePath)
            , StringConvert(entry.name)
            , StringConvert(errorCode.message())
        );
        return CacheReadStatus::Error;
    }

    outBytecode.clear();
    return CacheReadStatus::Miss;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool GetVariantBytecode(
    const ShaderCook::ShaderEntry& entry,
    const AStringView variantName,
    const ShaderCook::DefineCombo& defineCombo,
    const ShaderCook::CookVector<Path>& includeDirectories,
    const ShaderCook::CookVector<Path>& dependencies,
    const Path& sourcePath,
    const Path& bytecodeCachePath,
    const bool compilerInputsHaveBom,
    Core::GraphicsBytes& outBytecode,
    ScratchArena& scratchArena
){
    ErrorCode errorCode;

    outBytecode.clear();

    const CacheReadStatus::Enum bytecodeCacheStatus = TryReadCachedBytecode(entry, bytecodeCachePath, outBytecode);
    if(bytecodeCacheStatus == CacheReadStatus::Error)
        return false;
    if(bytecodeCacheStatus == CacheReadStatus::Hit)
        return true;

    HashMap<AStringView, AStringView, ScratchArena, Hasher<AStringView>, EqualTo<AStringView>> mergedDefines(
        0,
        Hasher<AStringView>(),
        EqualTo<AStringView>(),
        scratchArena
    );
    if(defineCombo.size() > Limit<usize>::s_Max - entry.implicitDefines.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: define count overflow for entry '{}'"), StringConvert(entry.name));
        return false;
    }

    const usize mergedDefineCapacity = defineCombo.size() + entry.implicitDefines.size();
    mergedDefines.reserve(mergedDefineCapacity);
    for(const auto& [defineName, value] : defineCombo)
        mergedDefines.insert_or_assign(AStringView(defineName), AStringView(value));
    for(const auto& [defineName, value] : entry.implicitDefines)
        mergedDefines.insert_or_assign(AStringView(defineName), AStringView(value));

    Vector<ShaderCook::ShaderMacroDefinition, ScratchArena> compileDefines{ scratchArena };
    if(mergedDefines.size() > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: entry '{}' has too many merged defines for shader compilation")
            , StringConvert(entry.name)
        );
        return false;
    }
    compileDefines.reserve(mergedDefines.size());
    for(const auto& [defineName, value] : mergedDefines)
        compileDefines.push_back(ShaderCook::ShaderMacroDefinition{ defineName, value });
    Sort(compileDefines.begin(), compileDefines.end(), [](const ShaderCook::ShaderMacroDefinition& lhs, const ShaderCook::ShaderMacroDefinition& rhs){
        return lhs.name < rhs.name;
    });

    errorCode.clear();
    if(!EnsureDirectories(bytecodeCachePath.parentPath(), errorCode)){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: failed to create cache directory '{}': {}")
            , PathToString<tchar>(bytecodeCachePath.parentPath())
            , StringConvert(errorCode.message())
        );
        return false;
    }

    const ShaderCook::ShaderCompilerRequest compileRequest = {
        .shaderName = entry.name,
        .stage = entry.stage.view(),
        .entryPoint = entry.entryPoint,
        .variantName = variantName,
        .defines = compileDefines.data(),
        .includeDirectories = includeDirectories,
        .dependencies = dependencies,
        .externallyPlannedMacroIncludes = AssetsGraphicsCsgShaderVariants::s_ExternallyPlannedMacroIncludes,
        .sourcePath = sourcePath,
        .outputPath = bytecodeCachePath,
        .defineCount = static_cast<u32>(compileDefines.size()),
        .optimizationLevel = entry.optimizationLevel,
        .rayQuery = entry.rayQuery,
        .compilerInputsHaveBom = compilerInputsHaveBom,
    };
    return SlangShaderCompiler::CompileVariant(compileRequest, outBytecode);
}


static bool ReserveShaderIndexRecords(
    const PreparedShaderVector& preparedEntries,
    Core::GraphicsVector<Core::ShaderArchive::Record>& outShaderIndexRecords,
    usize& outShaderRecordCount
){
    outShaderRecordCount = 0u;

    u64 shaderRecordCount = 0;
    for(const PreparedShaderEntry& preparedEntry : preparedEntries){
        if(shaderRecordCount > Limit<u64>::s_Max - preparedEntry.variantCount){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: shader record count overflow"));
            return false;
        }
        shaderRecordCount += preparedEntry.variantCount;
    }
    if(shaderRecordCount > static_cast<u64>(Limit<usize>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: shader record count exceeds container capacity"));
        return false;
    }

    outShaderIndexRecords.clear();
    outShaderRecordCount = static_cast<usize>(shaderRecordCount);
    outShaderIndexRecords.reserve(outShaderRecordCount);
    return true;
}

static bool AppendShaderIndexToManifest(
    ShaderCook::CookArena& cookArena,
    const Core::GraphicsVector<Core::ShaderArchive::Record>& shaderIndexRecords,
    Core::Assets::AssetsVolumeCookDetail::AssetVolumePackManifest& manifest,
    VirtualPathHashSet& inOutSeenVirtualPathHashes
){
    const Name& shaderIndexVirtualPath = Core::ShaderArchive::IndexVirtualPathName();
    if(!inOutSeenVirtualPathHashes.insert(shaderIndexVirtualPath.hash()).second){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: duplicate shader archive index virtual path '{}'"),
            StringConvert(shaderIndexVirtualPath.resolvedText())
        );
        return false;
    }

    Core::GraphicsBytes indexBinary{cookArena};
    if(!Core::ShaderArchive::SerializeIndex(shaderIndexRecords, indexBinary)){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: failed to serialize shader index"));
        return false;
    }
    if(Core::Assets::AssetsVolumeCookDetail::AppendPayloadBytesToManifest(manifest, shaderIndexVirtualPath, indexBinary))
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: failed to append shader index to manifest"));
    return false;
}

static u64 BuildShaderVariantCookKeyHash(
    const NameHash& virtualPathHash,
    const u64 sourceChecksum,
    const u64 bytecodeChecksum
){
    static constexpr u32 s_ShaderVariantCookKeyVersion = 2u;
    u64 hash = s_Fnv64OffsetBasis;
    Fnv64AppendValue(hash, s_ShaderVariantCookKeyVersion);
    Fnv64AppendValue(hash, virtualPathHash);
    Fnv64AppendValue(hash, sourceChecksum);
    Fnv64AppendValue(hash, bytecodeChecksum);
    return hash;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsGraphicsCookDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AppendPreparedShadersToManifest(
    ShaderCook::CookArena& cookArena,
    ShaderCook& shaderCook,
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    PreparedShaderVector& preparedEntries,
    Core::Assets::AssetsVolumeCookDetail::AssetVolumePackManifest& manifest,
    VirtualPathHashSet& inOutSeenVirtualPathHashes,
    ScratchArena& scratchArena
){
    Core::GraphicsVector<Core::ShaderArchive::Record> shaderIndexRecords{cookArena};
    usize shaderRecordCount = 0u;
    if(!__hidden_shader_volume_writer::ReserveShaderIndexRecords(preparedEntries, shaderIndexRecords, shaderRecordCount))
        return false;

    u64 compilerFingerprint = 0u;
    if(!preparedEntries.empty() && !SlangShaderCompiler::ComputeCompilerFingerprint(cacheDirectory, compilerFingerprint, scratchArena))
        return false;

    Core::GraphicsBytes cookedBytecode{cookArena};
    Core::GraphicsBytes shaderAssetPayload{cookArena};
    ShaderCook::CookVector<ShaderCook::DefineCombo> defineCombinations{ cookArena };
    shaderIndexRecords.clear();

    for(PreparedShaderEntry& preparedEntry : preparedEntries){
        ShaderCook::ShaderEntry& entry = preparedEntry.entry;
        const Name shaderName = ToName(entry.name);
        const Name stageName = ToName(entry.archiveStage.view());
        if(!shaderName || !stageName || entry.entryPoint.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Shader cook failed to canonicalize shader identity for '{}' stage '{}' entry point '{}'")
                , StringConvert(entry.name)
                , StringConvert(entry.archiveStage.view())
                , StringConvert(entry.entryPoint)
            );
            return false;
        }

        auto appendShaderVariant = [&](const ShaderCook::DefineCombo& defineCombo) -> bool{
            const CookString generatedVariantName = shaderCook.buildVariantName(defineCombo, scratchArena);

            const u64 sourceChecksum = shaderCook.computeSourceChecksum(
                entry,
                generatedVariantName,
                preparedEntry.dependencyChecksum,
                compilerFingerprint,
                scratchArena
            );
            const CookString sourceChecksumHex = FormatHex64A(cookArena, sourceChecksum);

            const Path bytecodeCachePath = __hidden_shader_volume_writer::BuildVariantCachePath(
                cacheDirectory,
                configurationSafeName,
                sourceChecksumHex,
                scratchArena
            );
            if(!__hidden_shader_volume_writer::GetVariantBytecode(
                entry,
                generatedVariantName,
                defineCombo,
                preparedEntry.includeDirectories,
                preparedEntry.dependencies,
                preparedEntry.sourcePath,
                bytecodeCachePath,
                preparedEntry.compilerInputsHaveBom,
                cookedBytecode,
                scratchArena
            ))
                return false;

            const Name virtualPath = Core::ShaderArchive::BuildVirtualPathName(shaderName, generatedVariantName, stageName);
            if(!virtualPath){
                NWB_LOGGER_ERROR(NWB_TEXT("Shader cook failed to build virtual path for '{}' stage '{}' variant '{}'")
                    , StringConvert(entry.name)
                    , StringConvert(entry.archiveStage.view())
                    , StringConvert(generatedVariantName)
                );
                return false;
            }

            const NameHash virtualPathHash = virtualPath.hash();
            if(!inOutSeenVirtualPathHashes.insert(virtualPathHash).second){
                NWB_LOGGER_ERROR(NWB_TEXT("Shader cook produced duplicate virtual path '{}' (entry='{}', variant='{}')")
                    , StringConvert(virtualPath.resolvedText())
                    , StringConvert(entry.name)
                    , StringConvert(generatedVariantName)
                );
                return false;
            }

            const u64 bytecodeChecksum = ComputeFnv64Bytes(cookedBytecode.data(), cookedBytecode.size());
            const u64 cookKeyHash = __hidden_shader_volume_writer::BuildShaderVariantCookKeyHash(
                virtualPathHash,
                sourceChecksum,
                bytecodeChecksum
            );
            const ShaderBinaryPayload::AssetPayloadEncodeFailure::Enum payloadFailure = ShaderBinaryPayload::EncodeAssetPayload(
                AStringView(entry.entryPoint),
                cookedBytecode,
                shaderAssetPayload
            );
            if(payloadFailure != ShaderBinaryPayload::AssetPayloadEncodeFailure::None){
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to package shader payload '{}' (failure {})")
                    , StringConvert(virtualPath.resolvedText())
                    , static_cast<u32>(payloadFailure)
                );
                return false;
            }
            if(!Core::Assets::AssetsVolumeCookDetail::AppendPayloadBytesToManifest(manifest, virtualPath, shaderAssetPayload, cookKeyHash)){
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to append shader payload '{}' to manifest"), StringConvert(virtualPath.resolvedText()));
                return false;
            }

            Core::ShaderArchive::Record record(cookArena);
            record.shaderName = shaderName;
            record.variantName = generatedVariantName;
            record.stage = stageName;
            record.sourceChecksum = sourceChecksum;
            record.bytecodeChecksum = bytecodeChecksum;
            record.virtualPathHash = virtualPathHash;
            if(shaderIndexRecords.size() >= shaderRecordCount){
                NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: shader record count exceeded prepared capacity"));
                return false;
            }
            shaderIndexRecords.push_back(Move(record));
            return true;
        };

        if(!shaderCook.expandDefineCombinations(entry.defineValues, defineCombinations, scratchArena)){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: variant combination count exceeds runtime limits for entry '{}'")
                , StringConvert(entry.name)
            );
            return false;
        }
        if(defineCombinations.empty())
            defineCombinations.push_back(ShaderCook::DefineCombo(cookArena));

        for(const ShaderCook::DefineCombo& defineCombo : defineCombinations){
            if(!appendShaderVariant(defineCombo))
                return false;

            if(preparedEntry.supportsCsgClipVariant || preparedEntry.supportsAvboitCsgClipVariant){
                ShaderCook::DefineCombo csgDefineCombo{0, Hasher<CookString>(), EqualTo<CookString>(), cookArena};
                if(!AssetsGraphicsCsgShaderVariants::BuildClipDefineCombo(cookArena, AStringView(entry.name), defineCombo, csgDefineCombo))
                    return false;

                if(!appendShaderVariant(csgDefineCombo))
                    return false;
            }
        }
    }

    if(shaderIndexRecords.size() != shaderRecordCount){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: shader record count mismatch after cook"));
        return false;
    }
    return __hidden_shader_volume_writer::AppendShaderIndexToManifest(
        cookArena,
        shaderIndexRecords,
        manifest,
        inOutSeenVirtualPathHashes
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

