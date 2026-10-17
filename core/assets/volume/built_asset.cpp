// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "built_asset.h"
#include "cooked_object_cache.h"

#include <core/common/log.h>

#include <global/binary.h>
#include <global/diagnostics.h>
#include <global/process.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_built_asset{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using ScratchArena = AssetsVolumeCookDetail::ScratchArena;
using ScratchBytes = Vector<u8, ScratchArena>;
using ScratchString = AString<ScratchArena>;
using ScratchPath = ::Path<ScratchArena>;

inline constexpr u32 s_Magic = 0x4142574eu;
inline constexpr u32 s_Version = 1u;
inline constexpr usize s_HeaderSize = sizeof(u32) * 2u + sizeof(NameHash) + sizeof(u64) * 2u;
inline constexpr AStringView s_TemporaryNamePrefix = ".nwb_";
inline constexpr char s_TemporaryNameSeparator = '_';
inline constexpr AStringView s_TemporaryNameExtension = ".tmp";
inline constexpr u64 s_TemporarySequenceStep = 1u;
Atomic<u64> g_TemporarySequence{0u};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool WriteIfChanged(const ScratchPath& path, const ScratchBytes& bytes, ScratchBytes& existing, const bool replaceDifferent = true){
    const auto read = ReadBinaryFile(path, existing);
    if(read && existing == bytes)
        return true;
    if(!replaceDifferent && (read || !IsMissingPathError(read.error()))){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: cannot publish built asset without replacing existing data '{}'"), PathToString<tchar>(path));
        return false;
    }

    ScratchString temporaryName(s_TemporaryNamePrefix, path.arena());
    AppendHexU64(CurrentProcessId(), temporaryName);
    temporaryName += s_TemporaryNameSeparator;
    AppendHexU64(g_TemporarySequence.fetch_add(s_TemporarySequenceStep, MemoryOrder::relaxed), temporaryName);
    temporaryName += s_TemporaryNameExtension;
    const ScratchPath temporary = path.parentPath() / temporaryName;
    ScopeExit removeTemporary([&temporary]()noexcept{
        if(!RemoveFile(temporary)){
            CaptureDiagnosticEvent(DiagnosticEventRecord{
                .event = "warning",
                .category = "built_asset_temporary_cleanup",
                .message = "AssetBuilder: failed to remove temporary output",
            });
        }
    });
    if(!WriteBinaryFile(temporary, bytes) || !ReadBinaryFile(temporary, existing) || existing != bytes || !RenamePath(temporary, path)){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: failed to publish built asset '{}'"), PathToString<tchar>(path));
        return false;
    }
    removeTemporary.release();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace BuiltAssetDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool WriteBuiltAssets(const Path& outputDirectory, const AssetsVolumeCookDetail::AssetVolumePackManifest& manifest, Alloc::ScratchArena& scratchArena){
    if(manifest.entries.size() != manifest.plannedFileCount || !EnsureDirectories(outputDirectory)){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: invalid build manifest or output directory '{}'"), PathToString<tchar>(outputDirectory));
        return false;
    }

    __hidden_built_asset::ScratchBytes objectBytes(scratchArena);
    __hidden_built_asset::ScratchBytes artifactBytes(scratchArena);
    __hidden_built_asset::ScratchBytes existingBytes(scratchArena);
    __hidden_built_asset::ScratchString manifestText(scratchArena);
    __hidden_built_asset::ScratchString filename(scratchArena);
    filename.reserve(NameDetail::s_HexDigitsPerHashLane * 3u + s_Extension.size());
    const __hidden_built_asset::ScratchPath scratchOutputDirectory(scratchArena, outputDirectory);
    __hidden_built_asset::ScratchPath path(scratchArena);
    usize manifestLineBytes = NameDetail::s_HexDigitsPerHashLane * 3u + s_Extension.size();
    usize manifestReserveBytes = 0u;
    if(AddBinaryReserveBytes(manifestLineBytes, 1u) && AddBinaryRepeatedReserveBytes(manifestReserveBytes, manifest.entries.size(), manifestLineBytes))
        manifestText.reserve(manifestReserveBytes);
    u64 artifactIndex = 0u;
    for(const auto& entry : manifest.entries){
        const u8* payloadData = entry.payloadBytes.data();
        usize payloadSize = entry.payloadBytes.size();
        if(entry.source == AssetsVolumeCookDetail::AssetVolumePackEntrySource::ObjectFilePayload){
            const auto payload = AssetsVolumeCookDetail::ReadCookedObjectPayload(entry.objectPath, entry.virtualPath, objectBytes);
            if(!payload)
                return false;
            if(payload->identity.payloadSize != entry.identity.payloadSize || payload->identity.payloadHash != entry.identity.payloadHash || payload->identity.cookKeyHash != entry.identity.cookKeyHash){
                NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: object cache identity mismatch '{}' for '{}'"), PathToString<tchar>(entry.objectPath), StringConvert(entry.virtualPath.resolvedText()));
                return false;
            }
            payloadData = payload->data.get();
            payloadSize = payload->size;
        }
        if(entry.identity.payloadSize != payloadSize || (entry.source != AssetsVolumeCookDetail::AssetVolumePackEntrySource::ObjectFilePayload && entry.identity.payloadHash != ComputeFnv64Bytes(payloadData, payloadSize))){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: invalid payload identity '{}'"), StringConvert(entry.virtualPath.resolvedText()));
            return false;
        }
        if(payloadSize > Limit<usize>::s_Max - __hidden_built_asset::s_HeaderSize){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: built asset payload is too large"));
            return false;
        }

        artifactBytes.clear();
        artifactBytes.reserve(__hidden_built_asset::s_HeaderSize + payloadSize);
        AppendPOD(artifactBytes, __hidden_built_asset::s_Magic);
        AppendPOD(artifactBytes, __hidden_built_asset::s_Version);
        AppendPOD(artifactBytes, entry.virtualPath.hash());
        AppendPOD(artifactBytes, entry.identity.payloadSize);
        AppendPOD(artifactBytes, entry.identity.payloadHash);
        BinaryDetail::AppendBytesNoReserveUnchecked(artifactBytes, payloadData, payloadSize);

        // Preserve files referenced by the previous manifest if a later entry fails during this publication.
        for(;;){
            if(artifactIndex == Limit<u64>::s_Max){
                NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: built asset filename slots are exhausted"));
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
                NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: failed to inspect built asset '{}'"), PathToString<tchar>(path));
                return false;
            }
            if(!__hidden_built_asset::WriteIfChanged(path, artifactBytes, existingBytes, false))
                return false;
            break;
        }
        manifestText += filename;
        manifestText += '\n';
    }

    artifactBytes.clear();
    artifactBytes.reserve(manifestText.size());
    BinaryDetail::AppendBytesNoReserveUnchecked(artifactBytes, manifestText.data(), manifestText.size());
    path = scratchOutputDirectory;
    path /= s_ManifestFilename;
    return __hidden_built_asset::WriteIfChanged(path, artifactBytes, existingBytes);
}

Expected<BuiltAssetPayload> ReadBuiltAsset(const Path& path, AssetBytes& inOutBytes){
    if(!ReadBinaryFile(path, inOutBytes) || inOutBytes.size() < __hidden_built_asset::s_HeaderSize){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetGatherer: invalid built asset '{}'"), PathToString<tchar>(path));
        return MakeUnexpected(Failure{});
    }

    usize cursor = 0u;
    const u32 magic = *ReadPOD<u32>(inOutBytes, cursor);
    const u32 version = *ReadPOD<u32>(inOutBytes, cursor);
    const NameHash virtualPathHash = *ReadPOD<NameHash>(inOutBytes, cursor);
    const u64 payloadSize = *ReadPOD<u64>(inOutBytes, cursor);
    const u64 payloadHash = *ReadPOD<u64>(inOutBytes, cursor);
    if(magic != __hidden_built_asset::s_Magic || version != __hidden_built_asset::s_Version || payloadSize != inOutBytes.size() - cursor || payloadHash != ComputeFnv64Bytes(inOutBytes.data() + cursor, inOutBytes.size() - cursor) || !Name(virtualPathHash)){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetGatherer: invalid built asset '{}'"), PathToString<tchar>(path));
        return MakeUnexpected(Failure{});
    }
    return BuiltAssetPayload{ .virtualPath = Name(virtualPathHash), .payloadOffset = cursor };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

