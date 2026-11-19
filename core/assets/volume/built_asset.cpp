// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "built_asset.h"
#include "asset_file_publisher.h"

#include <core/common/log.h>

#include <global/algorithm.h>
#include <global/binary.h>
#include <global/filesystem/utility.h>
#include <global/numeric_text.h>
#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_built_asset{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using ScratchBytes = Vector<u8, ScratchArena>;
using ScratchPath = ::Path<ScratchArena>;

inline constexpr usize s_FilenameHexFieldCount = 3u;

struct StandaloneManifestItem{
    usize nwbFileIndex = 0u;
    u32 nwbSubIndex = 0u;
    usize manifestEntryIndex = 0u;
};


[[nodiscard]] static b8 AppendManifestLine(const AStringView line, ScratchBytes& inOutManifestBytes){
    if(!IsSingleLinePathText(line))
        return false;

    AppendBytesUnchecked(inOutManifestBytes, line.data(), line.size());
    inOutManifestBytes.push_back(static_cast<u8>('\n'));
    return true;
}

[[nodiscard]] static b8 AppendCachePathToManifest(const ScratchPath& cachePath, ScratchArena& scratchArena, ScratchBytes& inOutManifestBytes){
    const ScratchString pathText = PathToGenericString(scratchArena, cachePath.lexicallyNormal());
    if(AppendManifestLine(AStringView(pathText), inOutManifestBytes))
        return true;

    NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetVolume: invalid cached asset path '{}'"), PathToString<tchar>(cachePath));
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace BuiltAssetDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 WriteBuiltAssets(
    const Path& outputDirectory,
    const Span<const NwbFileAssetCacheRecord> fileCacheRecords,
    const AssetsVolumeCookDetail::AssetVolumePackManifest& manifest,
    Alloc::ScratchArena& scratchArena
){
    if(manifest.entries.size() != manifest.plannedFileCount || !EnsureDirectories(outputDirectory)){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetVolume: invalid build manifest or output directory '{}'"), PathToString<tchar>(outputDirectory));
        return false;
    }

    for(const auto& entry : manifest.entries){
        const u8* const payloadData = entry.payloadBytes.data();
        const usize payloadSize = entry.payloadBytes.size();
        if(!entry.virtualPath || entry.identity.cookKeyHash == 0u || entry.identity.payloadSize != payloadSize || entry.identity.payloadHash != ComputeFnv64Bytes(payloadData, payloadSize)){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetVolume: invalid payload identity '{}'"), ConvertString(entry.virtualPath.resolvedText()));
            return false;
        }
    }

    __hidden_built_asset::ScratchBytes artifactBytes(scratchArena);
    __hidden_built_asset::ScratchBytes existingBytes(scratchArena);
    __hidden_built_asset::ScratchBytes manifestBytes(scratchArena);
    ScratchString filename(scratchArena);
    filename.reserve(s_HexU64DigitCount * __hidden_built_asset::s_FilenameHexFieldCount + s_Extension.size());
    const __hidden_built_asset::ScratchPath scratchOutputDirectory(scratchArena, outputDirectory);
    __hidden_built_asset::ScratchPath path(scratchArena);

    Vector<u8, ScratchArena> entryWrittenToCache(scratchArena);
    entryWrittenToCache.resize(manifest.entries.size(), 0u);

    Vector<__hidden_built_asset::StandaloneManifestItem, ScratchArena> standaloneItems(scratchArena);
    if(!fileCacheRecords.empty()){
        standaloneItems.reserve(manifest.entries.size());
        for(usize entryIndex = 0u; entryIndex < manifest.entries.size(); ++entryIndex){
            const auto& entry = manifest.entries[entryIndex];
            if(entry.nwbFileIndex >= fileCacheRecords.size())
                continue;
            const NwbFileAssetCacheRecord& record = fileCacheRecords[entry.nwbFileIndex];
            if(!record.standalone || !record.canWriteCache || record.loadedFromCache || record.cacheFilePath.empty())
                continue;
            entryWrittenToCache[entryIndex] = 1u;
            standaloneItems.push_back(__hidden_built_asset::StandaloneManifestItem{ entry.nwbFileIndex, entry.nwbSubIndex, entryIndex });
        }
        Sort(standaloneItems.begin(), standaloneItems.end(), [](const __hidden_built_asset::StandaloneManifestItem& lhs, const __hidden_built_asset::StandaloneManifestItem& rhs)noexcept{
            if(lhs.nwbFileIndex != rhs.nwbFileIndex)
                return lhs.nwbFileIndex < rhs.nwbFileIndex;
            return lhs.nwbSubIndex < rhs.nwbSubIndex;
        });
    }

    Vector<BuiltAssetEncodeEntry, ScratchArena> encodeEntries(scratchArena);
    usize itemCursor = 0u;
    for(usize fileIndex = 0u; fileIndex < fileCacheRecords.size(); ++fileIndex){
        while(itemCursor < standaloneItems.size() && standaloneItems[itemCursor].nwbFileIndex < fileIndex)
            ++itemCursor;
        const usize rangeBegin = itemCursor;
        while(itemCursor < standaloneItems.size() && standaloneItems[itemCursor].nwbFileIndex == fileIndex)
            ++itemCursor;
        const usize rangeEnd = itemCursor;

        const NwbFileAssetCacheRecord& record = fileCacheRecords[fileIndex];
        if(record.loadedFromCache){
            if(!record.standalone || record.cacheFilePath.empty()){
                NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetVolume: invalid cached standalone record"));
                return false;
            }
            path = __hidden_built_asset::ScratchPath(scratchArena, record.cacheFilePath);
            if(!__hidden_built_asset::AppendCachePathToManifest(path, scratchArena, manifestBytes))
                return false;
            continue;
        }
        if(rangeEnd <= rangeBegin)
            continue;

        const usize itemCount = rangeEnd - rangeBegin;
        if(itemCount > static_cast<usize>(Limit<u32>::s_Max)){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetVolume: built asset entry count exceeds format limit"));
            return false;
        }

        encodeEntries.clear();
        encodeEntries.reserve(itemCount);
        for(usize index = rangeBegin; index < rangeEnd; ++index){
            const __hidden_built_asset::StandaloneManifestItem& item = standaloneItems[index];
            const auto& entry = manifest.entries[item.manifestEntryIndex];
            encodeEntries.push_back(BuiltAssetEncodeEntry{
                .nwbSubIndex = item.nwbSubIndex,
                .assetTypeHash = entry.assetTypeHash,
                .virtualPath = entry.virtualPath,
                .payloadBytes = Span<const u8>(entry.payloadBytes.data(), entry.payloadBytes.size()),
                .payloadHashFnv64 = entry.identity.payloadHash,
            });
        }

        if(!BuiltAssetFormatDetail::EncodeBuiltAssetBytes(
            s_BuiltAssetFlagStandalone,
            record.paramSha256,
            AStringView(record.sourcePathText),
            encodeEntries,
            artifactBytes
        )){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetVolume: failed to encode standalone cached asset '{}'"), PathToString<tchar>(record.cacheFilePath));
            return false;
        }

        path = __hidden_built_asset::ScratchPath(scratchArena, record.cacheFilePath);
        if(!EnsureDirectories(path.parentPath()) || !AssetFilePublisher::Write(path, artifactBytes, true, existingBytes))
            return false;
        if(!__hidden_built_asset::AppendCachePathToManifest(path, scratchArena, manifestBytes))
            return false;
    }

    u64 artifactIndex = 0u;
    for(usize entryIndex = 0u; entryIndex < manifest.entries.size(); ++entryIndex){
        if(entryWrittenToCache[entryIndex] != 0u)
            continue;

        const auto& entry = manifest.entries[entryIndex];
        const BuiltAssetEncodeEntry singleEntry{
            .nwbSubIndex = 0u,
            .assetTypeHash = entry.assetTypeHash,
            .virtualPath = entry.virtualPath,
            .payloadBytes = Span<const u8>(entry.payloadBytes.data(), entry.payloadBytes.size()),
            .payloadHashFnv64 = entry.identity.payloadHash,
        };
        const Span<const BuiltAssetEncodeEntry> singleSpan(&singleEntry, 1u);
        if(!BuiltAssetFormatDetail::EncodeBuiltAssetBytes(0u, Sha256Digest{}, AStringView{}, singleSpan, artifactBytes)){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetVolume: built asset payload is too large"));
            return false;
        }

        for(;;){
            if(artifactIndex == Limit<u64>::s_Max){
                NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetVolume: built asset filename slots are exhausted"));
                return false;
            }
            filename.clear();
            AppendHexU64(artifactIndex++, filename);
            AppendHexU64(entry.identity.payloadHash, filename);
            AppendHexU64(entry.identity.cookKeyHash, filename);
            filename += s_Extension;
            path = scratchOutputDirectory;
            path /= filename;
            const auto read = ReadBinaryFile(path, existingBytes);
            if(read){
                if(existingBytes == artifactBytes)
                    break;
                continue;
            }
            if(!IsMissingPathError(read.error())){
                NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetVolume: failed to inspect built asset '{}'"), PathToString<tchar>(path));
                return false;
            }
            if(!AssetFilePublisher::Write(path, artifactBytes, false, existingBytes))
                return false;
            break;
        }
        if(!__hidden_built_asset::AppendManifestLine(AStringView(filename), manifestBytes))
            return false;
    }

    path = scratchOutputDirectory;
    path /= s_ManifestFilename;
    return AssetFilePublisher::Write(path, manifestBytes, true, existingBytes);
}

b8 WriteBuiltAssets(const Path& outputDirectory, const AssetsVolumeCookDetail::AssetVolumePackManifest& manifest, Alloc::ScratchArena& scratchArena){
    return WriteBuiltAssets(outputDirectory, Span<const NwbFileAssetCacheRecord>{}, manifest, scratchArena);
}

Expected<BuiltAssetPayloadVector> ReadBuiltAsset(const Path& path, AssetBytes& inOutBytes, Alloc::ScratchArena& scratchArena){
    BuiltAssetPayloadVector payloads(scratchArena);
    if(!ReadBinaryFile(path, inOutBytes)){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetVolume: invalid built asset '{}'"), PathToString<tchar>(path));
        return MakeUnexpected(Failure{});
    }

    DecodedBuiltAssetFileView decodedFile;
    Vector<DecodedBuiltAssetEntryView, ScratchArena> decodedEntries(scratchArena);
    if(!BuiltAssetFormatDetail::DecodeBuiltAssetBytes(inOutBytes, decodedFile, decodedEntries)){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetVolume: invalid built asset '{}'"), PathToString<tchar>(path));
        return MakeUnexpected(Failure{});
    }

    payloads.reserve(decodedEntries.size());
    for(const DecodedBuiltAssetEntryView& decoded : decodedEntries){
        payloads.push_back(BuiltAssetPayload{
            .virtualPath = decoded.virtualPath,
            .payloadOffset = decoded.payloadOffset,
            .payloadSize = decoded.payloadSize,
        });
    }
    return payloads;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

