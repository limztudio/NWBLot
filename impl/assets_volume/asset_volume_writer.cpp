// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset_volume_writer.h"

#include "cook_paths.h"

#include <core/assets/module.h>
#include <core/filesystem/module.h>

#include <core/common/log.h>

#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_asset_volume_writer{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using ScratchArena = AssetsVolumeCookDetail::ScratchArena;
using ScratchString = AssetsVolumeCookDetail::ScratchString;

static constexpr u64 s_DefaultSegmentSize = 512ull * 1024ull * 1024ull;
static constexpr u64 s_DefaultMetadataSize = 512ull * 1024ull;
static constexpr u32 s_ObjectFileMagic = 0x4f4a424e; // NBJO
static constexpr u16 s_ObjectFileVersion = 1u;

struct AssetVolumeObjectFileHeader{
    u32 magic = s_ObjectFileMagic;
    u16 version = s_ObjectFileVersion;
    u16 headerSize = sizeof(AssetVolumeObjectFileHeader);
    NameHash assetTypeHash = {};
    NameHash virtualPathHash = {};
    u64 payloadSize = 0u;
    u64 payloadHash = 0u;
};

struct CachedCookedAssetObject{
    Name virtualPath = NAME_NONE;
    Path objectPath;

    explicit CachedCookedAssetObject(Path::Arena& arena)
        : objectPath(arena)
    {}
    CachedCookedAssetObject(const Name& inVirtualPath, const Path& inObjectPath)
        : virtualPath(inVirtualPath)
        , objectPath(inObjectPath)
    {}
};

using CachedCookedAssetObjectVector = Core::Assets::CookVector<CachedCookedAssetObject>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static u64 EstimateRequiredMetadataBytes(const u64 fileCount){
    if(fileCount == 0)
        return s_DefaultMetadataSize;

    u64 totalBytes = 0;
    if(!Core::Filesystem::ComputeVolumeMetadataRequirement(fileCount, totalBytes))
        return Limit<u64>::s_Max;

    constexpr u64 s_MetadataPaddingBytes = 4ull * 1024ull;
    if(totalBytes <= Limit<u64>::s_Max - s_MetadataPaddingBytes)
        totalBytes += s_MetadataPaddingBytes;

    return Max(totalBytes, s_DefaultMetadataSize);
}

static bool ConfigureVolumeSizing(const u64 plannedFileCount, Core::Filesystem::VolumeBuildConfig& outConfig){
    if(!outConfig.volumeName.assign(AssetsVolumeCookDetail::s_AssetVolumeName)){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: volume name '{}' exceeds ACompactString capacity"), StringConvert(AssetsVolumeCookDetail::s_AssetVolumeName));
        return false;
    }
    outConfig.metadataSize = EstimateRequiredMetadataBytes(plannedFileCount);
    if(outConfig.metadataSize == Limit<u64>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: metadata size overflow while planning volume"));
        return false;
    }

    outConfig.segmentSize = s_DefaultSegmentSize;
    while(outConfig.segmentSize <= outConfig.metadataSize){
        if(outConfig.segmentSize > Limit<u64>::s_Max / 2ull){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: segment size overflow while planning volume"));
            return false;
        }
        outConfig.segmentSize *= 2ull;
    }

    return true;
}

static Path BuildObjectFilePath(
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const Name& virtualPath,
    ScratchArena& scratchArena
){
    ScratchString objectFileName = EncodeNameHash<char>(scratchArena, virtualPath);
    objectFileName += ".nwbobj";

    return cacheDirectory / configurationSafeName / "objects" / objectFileName;
}

static AssetVolumeObjectFileHeader BuildObjectFileHeader(
    const Core::Assets::IAssetCodec& codec,
    const Name& virtualPath,
    const Core::Assets::AssetBytes& payload
){
    AssetVolumeObjectFileHeader header;
    header.assetTypeHash = codec.assetType().hash();
    header.virtualPathHash = virtualPath.hash();
    header.payloadSize = static_cast<u64>(payload.size());
    header.payloadHash = ComputeFnv64Bytes(payload.data(), payload.size());
    return header;
}

static bool HeaderMatches(
    const AssetVolumeObjectFileHeader& lhs,
    const AssetVolumeObjectFileHeader& rhs
){
    return lhs.magic == rhs.magic
        && lhs.version == rhs.version
        && lhs.headerSize == rhs.headerSize
        && lhs.assetTypeHash == rhs.assetTypeHash
        && lhs.virtualPathHash == rhs.virtualPathHash
        && lhs.payloadSize == rhs.payloadSize
        && lhs.payloadHash == rhs.payloadHash
    ;
}

static bool ReadObjectFileHeader(
    const Path& objectPath,
    Core::Assets::AssetBytes& objectBytes,
    AssetVolumeObjectFileHeader& outHeader,
    usize& outPayloadOffset,
    const bool logReadFailure
){
    ErrorCode errorCode;
    if(!ReadBinaryFile(objectPath, objectBytes, errorCode)){
        if(logReadFailure && errorCode && !IsMissingPathError(errorCode)){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: failed to read object cache '{}': {}")
                , PathToString<tchar>(objectPath)
                , StringConvert(errorCode.message())
            );
        }
        return false;
    }

    usize cursor = 0u;
    if(!ReadPOD(objectBytes, cursor, outHeader))
        return false;
    if(outHeader.magic != s_ObjectFileMagic || outHeader.version != s_ObjectFileVersion)
        return false;
    if(outHeader.headerSize != sizeof(AssetVolumeObjectFileHeader))
        return false;

    if(outHeader.payloadSize > static_cast<u64>(Limit<usize>::s_Max))
        return false;
    const usize payloadSize = static_cast<usize>(outHeader.payloadSize);
    if(cursor > objectBytes.size() || objectBytes.size() - cursor != payloadSize)
        return false;
    if(outHeader.payloadHash != ComputeFnv64Bytes(objectBytes.data() + cursor, payloadSize))
        return false;

    outPayloadOffset = cursor;
    return true;
}

static bool WriteObjectFile(
    const Path& objectPath,
    const AssetVolumeObjectFileHeader& header,
    const Core::Assets::AssetBytes& payload,
    Core::Assets::AssetBytes& objectBytes
){
    if(payload.size() > Limit<usize>::s_Max - sizeof(AssetVolumeObjectFileHeader)){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: object cache payload is too large for '{}'"), PathToString<tchar>(objectPath));
        return false;
    }

    objectBytes.clear();
    objectBytes.reserve(sizeof(AssetVolumeObjectFileHeader) + payload.size());
    AppendPOD(objectBytes, header);
    BinaryDetail::AppendBytesNoReserveUnchecked(objectBytes, payload.data(), payload.size());

    ErrorCode errorCode;
    if(!EnsureDirectories(objectPath.parent_path(), errorCode)){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: failed to create object cache directory '{}': {}")
            , PathToString<tchar>(objectPath.parent_path())
            , StringConvert(errorCode.message())
        );
        return false;
    }
    if(WriteBinaryFile(objectPath, objectBytes))
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: failed to write object cache '{}'"), PathToString<tchar>(objectPath));
    return false;
}

static bool UpdateObjectFileIfChanged(
    const Path& objectPath,
    const AssetVolumeObjectFileHeader& header,
    const Core::Assets::AssetBytes& payload,
    Core::Assets::AssetBytes& objectBytes
){
    AssetVolumeObjectFileHeader cachedHeader;
    usize payloadOffset = 0u;
    if(ReadObjectFileHeader(objectPath, objectBytes, cachedHeader, payloadOffset, true) && HeaderMatches(cachedHeader, header))
        return true;

    return WriteObjectFile(objectPath, header, payload, objectBytes);
}

class AssetVolumeObjectCacheWriter final : public Core::Assets::ICookedAssetWriter{
public:
    AssetVolumeObjectCacheWriter(
        Core::Assets::AssetArena& arena,
        const Path& cacheDirectory,
        const AStringView configurationSafeName,
        CachedCookedAssetObjectVector& cachedObjects,
        ScratchArena& scratchArena
    )
        : m_cacheDirectory(cacheDirectory)
        , m_configurationSafeName(configurationSafeName)
        , m_cachedObjects(cachedObjects)
        , m_scratchArena(scratchArena)
        , m_payloadBinary(arena)
        , m_objectBinary(arena)
    {}

public:
    virtual bool writeCookedAsset(
        const tchar* assetKind,
        const Name& virtualPath,
        const Core::Assets::IAsset& asset,
        const Core::Assets::IAssetCodec& codec
    )override{
        m_payloadBinary.clear();
        if(!codec.serialize(asset, m_payloadBinary)){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: failed to serialize {} '{}'")
                , assetKind
                , StringConvert(virtualPath.c_str())
            );
            return false;
        }

        const Path objectPath = BuildObjectFilePath(
            m_cacheDirectory,
            m_configurationSafeName,
            virtualPath,
            m_scratchArena
        );
        const AssetVolumeObjectFileHeader header = BuildObjectFileHeader(codec, virtualPath, m_payloadBinary);
        if(!UpdateObjectFileIfChanged(objectPath, header, m_payloadBinary, m_objectBinary))
            return false;

        m_cachedObjects.emplace_back(virtualPath, objectPath);
        return true;
    }

private:
    const Path& m_cacheDirectory;
    AStringView m_configurationSafeName;
    CachedCookedAssetObjectVector& m_cachedObjects;
    ScratchArena& m_scratchArena;
    Core::Assets::AssetBytes m_payloadBinary;
    Core::Assets::AssetBytes m_objectBinary;
};

static bool PushObjectFileToVolume(
    const CachedCookedAssetObject& object,
    Core::Assets::AssetBytes& objectBytes,
    Core::Filesystem::VolumeSession& volumeSession
){
    AssetVolumeObjectFileHeader header;
    usize payloadOffset = 0u;
    if(!ReadObjectFileHeader(object.objectPath, objectBytes, header, payloadOffset, true)){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: invalid object cache '{}' for '{}'")
            , PathToString<tchar>(object.objectPath)
            , StringConvert(object.virtualPath.c_str())
        );
        return false;
    }
    if(header.virtualPathHash != object.virtualPath.hash()){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: object cache virtual path mismatch '{}'"), PathToString<tchar>(object.objectPath));
        return false;
    }

    if(volumeSession.pushDataDeferred(
        object.virtualPath,
        objectBytes.data() + payloadOffset,
        static_cast<usize>(header.payloadSize)
    ))
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: failed to push cached asset '{}'"), StringConvert(object.virtualPath.c_str()));
    return false;
}

static bool PushObjectFilesToVolume(
    Core::Assets::AssetArena& arena,
    const CachedCookedAssetObjectVector& cachedObjects,
    Core::Filesystem::VolumeSession& volumeSession
){
    Core::Assets::AssetBytes objectBytes(arena);
    for(const CachedCookedAssetObject& object : cachedObjects){
        if(!PushObjectFileToVolume(object, objectBytes, volumeSession))
            return false;
    }

    return true;
}

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsVolumeCookDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool WriteAssetVolume(
    Core::Alloc::GlobalArena& arena,
    const ResolvedCookPaths& resolvedPaths,
    const AStringView configurationSafeName,
    const u64 plannedFileCount,
    const AssetVolumeExternalWriterVector& externalWriters,
    ParsedAssetMetadata& parsedMetadata,
    AssetVolumeWriteResult& outResult,
    ScratchArena& scratchArena
){
    outResult = {};

    VirtualPathHashSet seenVirtualPathHashes{arena};
    if(plannedFileCount <= static_cast<u64>(Limit<usize>::s_Max))
        seenVirtualPathHashes.reserve(static_cast<usize>(plannedFileCount));

    Core::Filesystem::VolumeBuildConfig volumeConfig;
    if(!__hidden_asset_volume_writer::ConfigureVolumeSizing(plannedFileCount, volumeConfig))
        return false;

    const StagedVolumePaths stagedVolumePaths = BuildStagedVolumePaths(
        resolvedPaths.outputDirectory,
        volumeConfig.volumeName,
        configurationSafeName,
        scratchArena
    );
    if(!Core::Filesystem::EnsureEmptyStagedDirectory(
        stagedVolumePaths.stageDirectory,
        s_AssetVolumeCookerLogPrefix,
        "stage directory"
    ))
        return false;
    Core::Filesystem::StagedDirectoryCleanupGuard stageDirectoryCleanup(
        stagedVolumePaths.stageDirectory,
        s_AssetVolumeCookerLogPrefix
    );
    if(!Core::Filesystem::RemoveStagedDirectoryIfPresent(
        stagedVolumePaths.backupDirectory,
        s_AssetVolumeCookerLogPrefix,
        "backup directory"
    ))
        return false;

    u64 stagedFileCount = 0;
    usize stagedSegmentCount = 0;
    {
        Core::Filesystem::VolumeSession volumeSession(arena);
        if(!volumeSession.create(stagedVolumePaths.stageDirectory, volumeConfig)){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: failed to create staged volume session"));
            return false;
        }

        for(const AssetVolumeExternalWriter& externalWriter : externalWriters){
            if(!externalWriter(volumeSession, seenVirtualPathHashes, scratchArena))
                return false;
        }

        __hidden_asset_volume_writer::CachedCookedAssetObjectVector cachedObjects(arena);
        const u64 registryEntryCount = parsedMetadata.entryRegistry.entryCount();
        if(registryEntryCount <= static_cast<u64>(Limit<usize>::s_Max))
            cachedObjects.reserve(static_cast<usize>(registryEntryCount));
        __hidden_asset_volume_writer::AssetVolumeObjectCacheWriter cookedAssetWriter(
            arena,
            resolvedPaths.cacheDirectory,
            configurationSafeName,
            cachedObjects,
            scratchArena
        );
        Core::Assets::CookEntryWriteContext cookEntryWriteContext{
            cookedAssetWriter,
            seenVirtualPathHashes
        };
        if(!parsedMetadata.entryRegistry.writeAll(cookEntryWriteContext))
            return false;
        if(!__hidden_asset_volume_writer::PushObjectFilesToVolume(arena, cachedObjects, volumeSession))
            return false;
        if(!volumeSession.flush()){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: failed to flush staged volume metadata"));
            return false;
        }

        stagedFileCount = volumeSession.fileCount();
        stagedSegmentCount = volumeSession.segmentCount();
    }

    if(!Core::Filesystem::PublishStagedVolume(stagedVolumePaths, resolvedPaths.outputDirectory, volumeConfig.volumeName, stagedSegmentCount))
        return false;
    stageDirectoryCleanup.dismiss();

    if(!outResult.volumeName.assign(s_AssetVolumeName)){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: volume name exceeds ACompactString capacity"));
        return false;
    }
    outResult.fileCount = stagedFileCount;
    outResult.segmentCount = static_cast<u64>(stagedSegmentCount);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

