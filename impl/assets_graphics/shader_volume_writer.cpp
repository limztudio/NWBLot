// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shader_volume_writer.h"

#include "csg_shader_variants.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_shader_volume_writer{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace AssetsGraphicsCookDetail;

struct VariantCachePaths{
    Path bytecodePath;
    Path sourceChecksumPath;
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

    VariantCachePaths cachePaths;
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
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: failed to read checksum cache '{}' for entry '{}': {}")
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
    if(ReadBinaryFile(bytecodePath, outBytecode, errorCode) && !outBytecode.empty() && (outBytecode.size() & 3u) == 0u)
        return CacheReadStatus::Hit;

    if(errorCode && !IsMissingPathError(errorCode)){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: failed to read bytecode cache '{}' for entry '{}': {}")
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
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: define count overflow for entry '{}'"), StringConvert(entry.name));
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
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: entry '{}' has too many merged defines for shader compilation")
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
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: failed to create cache directory '{}': {}")
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsGraphicsCookDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AppendPreparedShadersToVolume(
    ShaderCook::CookArena& cookArena,
    ShaderCook& shaderCook,
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    PreparedShaderVector& preparedEntries,
    Core::Filesystem::VolumeSession& volumeSession,
    VirtualPathHashSet& inOutSeenVirtualPathHashes,
    const usize shaderRecordCount,
    Core::GraphicsVector<Core::ShaderArchive::Record>& outShaderIndexRecords,
    ScratchArena& scratchArena
){
    Core::GraphicsBytes cookedBytecode{cookArena};
    ShaderCook::CookVector<ShaderCook::DefineCombo> defineCombinations{ cookArena };
    outShaderIndexRecords.clear();

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

            if(!volumeSession.pushDataDeferred(virtualPath, cookedBytecode)){
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to push shader bytecode '{}'"), StringConvert(virtualPath.c_str()));
                return false;
            }

            Core::ShaderArchive::Record record(cookArena);
            record.shaderName = shaderName;
            record.variantName = generatedVariantName;
            record.stage = stageName;
            record.sourceChecksum = sourceChecksum;
            record.bytecodeChecksum = ComputeFnv64Bytes(cookedBytecode.data(), cookedBytecode.size());
            record.virtualPathHash = virtualPathHash;
            if(outShaderIndexRecords.size() >= shaderRecordCount){
                NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: shader record count exceeded prepared capacity"));
                return false;
            }
            outShaderIndexRecords.push_back(Move(record));
            return true;
        };

        if(!shaderCook.expandDefineCombinations(entry.defineValues, defineCombinations, scratchArena)){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: variant combination count exceeds runtime limits for entry '{}'")
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

    if(outShaderIndexRecords.size() != shaderRecordCount){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: shader record count mismatch after cook"));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

