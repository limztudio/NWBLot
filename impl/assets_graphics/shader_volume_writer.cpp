// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shader_volume_writer.h"

#include "csg_shader_variants.h"

#include <impl/assets_shader/binary_payload.h>

#include <global/core/graphics/shader_archive.h>
#include <global/core/filesystem/module.h>

#include <global/core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_shader_volume_writer{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace AssetsGraphicsCookDetail;

struct VariantCachePaths{
    Path bytecodePath;
    Path sourceChecksumPath;

    explicit VariantCachePaths(Path::Arena& arena)
        : bytecodePath(arena)
        , sourceChecksumPath(arena)
    {}
};

namespace CacheReadStatus{
enum Enum : u8{
    Hit = 0,
    Miss,
    Error
};
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static VariantCachePaths BuildVariantCachePaths(
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const AStringView shaderSafeName,
    const AStringView stageSafeName,
    const AStringView variantName,
    ScratchArena& scratchArena
){
    ScratchString bytecodeFileName{scratchArena};
    bytecodeFileName.reserve(shaderSafeName.size() + 2u + stageSafeName.size() + 2u + 16u + 4u);
    bytecodeFileName += shaderSafeName;
    bytecodeFileName += "__";
    bytecodeFileName += stageSafeName;
    bytecodeFileName += "__";
    AppendHexU64(ComputeFnv64Text(variantName), bytecodeFileName);
    bytecodeFileName += ".spv";

    VariantCachePaths cachePaths(cacheDirectory.arena());
    cachePaths.bytecodePath = cacheDirectory / configurationSafeName / bytecodeFileName;
    cachePaths.sourceChecksumPath = cachePaths.bytecodePath;
    cachePaths.sourceChecksumPath += ".source";
    return cachePaths;
}

static CacheReadStatus::Enum TryReadCachedSourceChecksum(
    const ShaderCook::ShaderEntry& entry,
    const Path& sourceChecksumPath,
    ScratchString& outCachedText
){
    ErrorCode errorCode;
    if(ReadBinaryFile(sourceChecksumPath, outCachedText, errorCode))
        return CacheReadStatus::Hit;

    if(errorCode && !IsMissingPathError(errorCode)){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: failed to read checksum cache '{}' for entry '{}': {}")
            , PathToString<tchar>(sourceChecksumPath)
            , StringConvert(entry.name)
            , StringConvert(errorCode.message())
        );
        return CacheReadStatus::Error;
    }

    return CacheReadStatus::Miss;
}

static CacheReadStatus::Enum TryReadCachedBytecode(
    const ShaderCook::ShaderEntry& entry,
    const Path& bytecodePath,
    Core::GraphicsBytes& outBytecode
){
    ErrorCode errorCode;
    if(ReadBinaryFile(bytecodePath, outBytecode, errorCode)){
        if(ShaderBinaryPayload::ValidateBytecode(outBytecode) == ShaderBinaryPayload::BytecodeValidationFailure::None)
            return CacheReadStatus::Hit;

        outBytecode.clear();
        return CacheReadStatus::Miss;
    }

    if(errorCode && !IsMissingPathError(errorCode)){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: failed to read bytecode cache '{}' for entry '{}': {}")
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
    const Path& sourcePath,
    const VariantCachePaths& cachePaths,
    const AStringView sourceChecksumHex,
    ShaderCook& shaderCook,
    Core::GraphicsBytes& outBytecode,
    ScratchArena& scratchArena
){
    ErrorCode errorCode;

    outBytecode.clear();

    ScratchString cachedText{scratchArena};
    const CacheReadStatus::Enum checksumCacheStatus = __hidden_shader_volume_writer::TryReadCachedSourceChecksum(
        entry,
        cachePaths.sourceChecksumPath,
        cachedText
    );
    if(checksumCacheStatus == CacheReadStatus::Error)
        return false;

    if(checksumCacheStatus == CacheReadStatus::Hit && TrimView(cachedText) == AStringView(sourceChecksumHex.data(), sourceChecksumHex.size())){
        const CacheReadStatus::Enum bytecodeCacheStatus = __hidden_shader_volume_writer::TryReadCachedBytecode(
            entry,
            cachePaths.bytecodePath,
            outBytecode
        );
        if(bytecodeCacheStatus == CacheReadStatus::Error)
            return false;
        if(bytecodeCacheStatus == CacheReadStatus::Hit)
            return true;
    }

    HashMap<
        AStringView,
        AStringView,
        Hasher<AStringView>,
        EqualTo<AStringView>,
        ScratchArena
    > mergedDefines(
        0,
        Hasher<AStringView>(),
        EqualTo<AStringView>(),
        scratchArena
    );
    if(defineCombo.size() > Limit<usize>::s_Max - entry.implicitDefines.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: define count overflow for entry '{}'"), StringConvert(entry.name));
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
        NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: entry '{}' has too many merged defines for shader compilation")
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
    if(!EnsureDirectories(cachePaths.bytecodePath.parent_path(), errorCode)){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: failed to create cache directory '{}': {}")
            , PathToString<tchar>(cachePaths.bytecodePath.parent_path())
            , StringConvert(errorCode.message())
        );
        return false;
    }

    const ShaderCook::ShaderCompilerRequest compileRequest = {
        entry.name,
        entry.stage.view(),
        entry.targetProfile.view(),
        entry.entryPoint,
        variantName,
        compileDefines.data(),
        static_cast<u32>(compileDefines.size()),
        includeDirectories,
        sourcePath,
        cachePaths.bytecodePath
    };
    if(!shaderCook.compileVariant(compileRequest, outBytecode))
        return false;

    if(!WriteTextFile(cachePaths.sourceChecksumPath, sourceChecksumHex)){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write cook source checksum '{}'")
            , PathToString<tchar>(cachePaths.sourceChecksumPath)
        );
        return false;
    }

    return true;
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
            NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: shader record count overflow"));
            return false;
        }
        shaderRecordCount += preparedEntry.variantCount;
    }
    if(shaderRecordCount > static_cast<u64>(Limit<usize>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: shader record count exceeds container capacity"));
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
        NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: duplicate shader archive index virtual path '{}'"),
            StringConvert(shaderIndexVirtualPath.c_str())
        );
        return false;
    }

    Core::GraphicsBytes indexBinary{cookArena};
    if(!Core::ShaderArchive::serializeIndex(shaderIndexRecords, indexBinary)){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: failed to serialize shader index"));
        return false;
    }
    if(Core::Assets::AssetsVolumeCookDetail::AppendPayloadBytesToManifest(manifest, shaderIndexVirtualPath, indexBinary))
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: failed to append shader index to manifest"));
    return false;
}

static u64 BuildShaderVariantCookKeyHash(
    const NameHash& virtualPathHash,
    const u64 sourceChecksum,
    const u64 bytecodeChecksum
){
    static constexpr u32 s_ShaderVariantCookKeyVersion = 1u;
    u64 hash = FNV64_OFFSET_BASIS;
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

    Core::GraphicsBytes cookedBytecode{cookArena};
    ShaderCook::CookVector<ShaderCook::DefineCombo> defineCombinations{ cookArena };
    shaderIndexRecords.clear();

    for(PreparedShaderEntry& preparedEntry : preparedEntries){
        ShaderCook::ShaderEntry& entry = preparedEntry.entry;
        const Name shaderName = ToName(entry.name);
        const Name stageName = ToName(entry.archiveStage.view());
        if(!shaderName || !stageName || entry.entryPoint.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Shader cook failed to canonicalize shader identity for '{}' stage '{}' entry point '{}'")
                , StringConvert(entry.name)
                , StringConvert(entry.archiveStage.c_str())
                , StringConvert(entry.entryPoint)
            );
            return false;
        }

        const CookString shaderSafeName = BuildSafeCacheName(entry.name);
        const CookString stageSafeName = BuildSafeCacheName(cookArena, entry.archiveStage.view());

        auto appendShaderVariant = [&](const ShaderCook::DefineCombo& defineCombo) -> bool{
            const CookString generatedVariantName = shaderCook.buildVariantName(defineCombo, scratchArena);

            u64 sourceChecksum = 0;
            if(!shaderCook.computeSourceChecksum(
                entry,
                generatedVariantName,
                preparedEntry.dependencyChecksum,
                sourceChecksum,
                scratchArena
            ))
                return false;
            const CookString sourceChecksumHex = FormatHex64(cookArena, sourceChecksum);

            const __hidden_shader_volume_writer::VariantCachePaths cachePaths = __hidden_shader_volume_writer::BuildVariantCachePaths(
                cacheDirectory,
                configurationSafeName,
                shaderSafeName,
                stageSafeName,
                generatedVariantName,
                scratchArena
            );
            if(!__hidden_shader_volume_writer::GetVariantBytecode(
                entry,
                generatedVariantName,
                defineCombo,
                preparedEntry.includeDirectories,
                preparedEntry.sourcePath,
                cachePaths,
                sourceChecksumHex,
                shaderCook,
                cookedBytecode,
                scratchArena
            ))
                return false;

            const Name virtualPath = Core::ShaderArchive::buildVirtualPathName(shaderName, generatedVariantName, stageName);
            if(!virtualPath){
                NWB_LOGGER_ERROR(NWB_TEXT("Shader cook failed to build virtual path for '{}' stage '{}' variant '{}'")
                    , StringConvert(entry.name)
                    , StringConvert(entry.archiveStage.c_str())
                    , StringConvert(generatedVariantName)
                );
                return false;
            }

            const NameHash virtualPathHash = virtualPath.hash();
            if(!inOutSeenVirtualPathHashes.insert(virtualPathHash).second){
                NWB_LOGGER_ERROR(NWB_TEXT("Shader cook produced duplicate virtual path '{}' (entry='{}', variant='{}')")
                    , StringConvert(virtualPath.c_str())
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
            if(!Core::Assets::AssetsVolumeCookDetail::AppendPayloadBytesToManifest(manifest, virtualPath, cookedBytecode, cookKeyHash)){
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to append shader bytecode '{}' to manifest"), StringConvert(virtualPath.c_str()));
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
                NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: shader record count exceeded prepared capacity"));
                return false;
            }
            shaderIndexRecords.push_back(Move(record));
            return true;
        };

        if(!shaderCook.expandDefineCombinations(entry.defineValues, defineCombinations, scratchArena)){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: variant combination count exceeds runtime limits for entry '{}'")
                , StringConvert(entry.name)
            );
            return false;
        }
        if(defineCombinations.empty())
            defineCombinations.push_back(ShaderCook::DefineCombo(cookArena));

        for(const ShaderCook::DefineCombo& defineCombo : defineCombinations){
            if(!appendShaderVariant(defineCombo))
                return false;

            if(preparedEntry.supportsCsgClipVariant){
                ShaderCook::DefineCombo csgDefineCombo{0, Hasher<CookString>(), EqualTo<CookString>(), cookArena};
                if(!AssetsGraphicsCsgShaderVariants::BuildClipDefineCombo(cookArena, AStringView(entry.name), defineCombo, csgDefineCombo))
                    return false;

                if(!appendShaderVariant(csgDefineCombo))
                    return false;
            }

            if(preparedEntry.supportsAvboitCsgClipVariant){
                ShaderCook::DefineCombo avboitCsgDefineCombo{0, Hasher<CookString>(), EqualTo<CookString>(), cookArena};
                if(!AssetsGraphicsCsgShaderVariants::BuildAvboitClipDefineCombo(cookArena, AStringView(entry.name), defineCombo, avboitCsgDefineCombo))
                    return false;

                if(!appendShaderVariant(avboitCsgDefineCombo))
                    return false;
            }
        }
    }

    if(shaderIndexRecords.size() != shaderRecordCount){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: shader record count mismatch after cook"));
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

