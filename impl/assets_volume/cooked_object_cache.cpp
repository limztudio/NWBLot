// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cooked_object_cache.h"

#include <core/assets/module.h>
#include <core/filesystem/module.h>

#include <core/common/log.h>

#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cooked_object_cache{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using ScratchArena = AssetsVolumeCookDetail::ScratchArena;
using ScratchString = AssetsVolumeCookDetail::ScratchString;

static constexpr u32 s_ObjectFileMagic = 0x4f4a424e; // NBJO
static constexpr u16 s_ObjectFileVersion = 2u;

struct AssetVolumeObjectFileHeader{
    u32 magic = s_ObjectFileMagic;
    u16 version = s_ObjectFileVersion;
    u16 headerSize = sizeof(AssetVolumeObjectFileHeader);
    NameHash assetTypeHash = {};
    u64 payloadSize = 0u;
    u64 payloadHash = 0u;
    u64 cookKeyHash = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void AppendEncodedNameHashText(const NameHash& hash, ScratchString& outText){
    for(u32 lane = 0u; lane < NameDetail::s_HashLaneCount; ++lane)
        AppendHexU64(hash.qwords[lane], outText);
}

static Path BuildObjectFilePath(
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const AssetVolumeObjectFileHeader& header,
    ScratchArena& scratchArena
){
    ScratchString assetTypeDirectory{scratchArena};
    assetTypeDirectory.reserve(NameDetail::s_EncodedNameHashLength);
    AppendEncodedNameHashText(header.assetTypeHash, assetTypeDirectory);

    ScratchString objectFileName{scratchArena};
    objectFileName.reserve(2u + 1u + 16u + 2u + 16u + 2u + 16u + 7u);
    objectFileName += "v2_";
    AppendHexU64(header.cookKeyHash, objectFileName);
    objectFileName += "__";
    AppendHexU64(header.payloadHash, objectFileName);
    objectFileName += "__";
    AppendHexU64(header.payloadSize, objectFileName);
    objectFileName += ".nwbobj";

    return cacheDirectory / configurationSafeName / "objects" / assetTypeDirectory / objectFileName;
}

static AssetVolumeObjectFileHeader BuildObjectFileHeader(
    const Core::Assets::IAssetCodec& codec,
    const Core::Assets::AssetBytes& payload,
    const AStringView configurationSafeName
){
    static constexpr u32 s_ObjectCookKeyVersion = 1u;

    AssetVolumeObjectFileHeader header;
    header.assetTypeHash = codec.assetType().hash();
    header.payloadSize = static_cast<u64>(payload.size());
    header.payloadHash = ComputeFnv64Bytes(payload.data(), payload.size());
    const u64 configurationHash = ComputeFnv64Text(configurationSafeName);
    u64 cookKeyHash = FNV64_OFFSET_BASIS;
    Fnv64AppendValue(cookKeyHash, s_ObjectCookKeyVersion);
    Fnv64AppendValue(cookKeyHash, s_ObjectFileVersion);
    Fnv64AppendValue(cookKeyHash, configurationHash);
    Fnv64AppendValue(cookKeyHash, header.assetTypeHash);
    Fnv64AppendValue(cookKeyHash, header.payloadSize);
    Fnv64AppendValue(cookKeyHash, header.payloadHash);
    header.cookKeyHash = cookKeyHash;
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
        && lhs.payloadSize == rhs.payloadSize
        && lhs.payloadHash == rhs.payloadHash
        && lhs.cookKeyHash == rhs.cookKeyHash
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
        AssetsVolumeCookDetail::AssetVolumePackManifest& manifest,
        ScratchArena& scratchArena
    )
        : m_cacheDirectory(cacheDirectory)
        , m_configurationSafeName(configurationSafeName)
        , m_manifest(manifest)
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

        const AssetVolumeObjectFileHeader header = BuildObjectFileHeader(codec, m_payloadBinary, m_configurationSafeName);
        const Path objectPath = BuildObjectFilePath(
            m_cacheDirectory,
            m_configurationSafeName,
            header,
            m_scratchArena
        );
        if(!UpdateObjectFileIfChanged(objectPath, header, m_payloadBinary, m_objectBinary))
            return false;

        return AssetsVolumeCookDetail::AppendObjectFilePayloadToManifest(
            m_manifest,
            virtualPath,
            objectPath,
            AssetsVolumeCookDetail::AssetVolumePayloadIdentity{
                header.payloadSize,
                header.payloadHash,
                header.cookKeyHash
            }
        );
    }

private:
    const Path& m_cacheDirectory;
    AStringView m_configurationSafeName;
    AssetsVolumeCookDetail::AssetVolumePackManifest& m_manifest;
    ScratchArena& m_scratchArena;
    Core::Assets::AssetBytes m_payloadBinary;
    Core::Assets::AssetBytes m_objectBinary;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsVolumeCookDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool BuildRegistryObjectManifestEntries(
    Core::Alloc::GlobalArena& arena,
    const ResolvedCookPaths& resolvedPaths,
    const AStringView configurationSafeName,
    ParsedAssetMetadata& parsedMetadata,
    AssetVolumePackManifest& manifest,
    VirtualPathHashSet& seenVirtualPathHashes,
    ScratchArena& scratchArena
){
    __hidden_cooked_object_cache::AssetVolumeObjectCacheWriter cookedAssetWriter(
        arena,
        resolvedPaths.cacheDirectory,
        configurationSafeName,
        manifest,
        scratchArena
    );
    Core::Assets::CookEntryWriteContext cookEntryWriteContext{
        cookedAssetWriter,
        seenVirtualPathHashes
    };
    return parsedMetadata.entryRegistry.writeAll(cookEntryWriteContext);
}

bool ReadCookedObjectPayload(
    const Path& objectPath,
    const Name& expectedVirtualPath,
    Core::Assets::AssetBytes& objectBytes,
    CookedObjectPayloadView& outPayload
){
    outPayload = {};

    __hidden_cooked_object_cache::AssetVolumeObjectFileHeader header;
    usize payloadOffset = 0u;
    if(!__hidden_cooked_object_cache::ReadObjectFileHeader(objectPath, objectBytes, header, payloadOffset, true)){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: invalid object cache '{}' for '{}'")
            , PathToString<tchar>(objectPath)
            , StringConvert(expectedVirtualPath.c_str())
        );
        return false;
    }
    outPayload.data = objectBytes.data() + payloadOffset;
    outPayload.size = static_cast<usize>(header.payloadSize);
    outPayload.identity = AssetVolumePayloadIdentity{
        header.payloadSize,
        header.payloadHash,
        header.cookKeyHash
    };
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
