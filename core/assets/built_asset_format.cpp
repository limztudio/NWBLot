// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "built_asset_format.h"

#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace BuiltAssetFormatDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 EncodeBuiltAssetBytes(
    const u32 flags,
    const Sha256Digest& paramSha256,
    const AStringView sourcePathText,
    const Span<const BuiltAssetEncodeEntry> entries,
    Vector<u8, ScratchArena>& outBytes
){
    outBytes.clear();
    if(entries.empty() || entries.size() > static_cast<usize>(Limit<u32>::s_Max))
        return false;

    usize reserveBytes = sizeof(BuiltAssetFileHeader);
    if(AddBinaryStringReserveBytes(reserveBytes, sourcePathText)){
        for(const BuiltAssetEncodeEntry& entry : entries){
            const AStringView virtualPathText = entry.virtualPath.resolvedText();
            usize entryBytes = sizeof(u32) + sizeof(NameHash) * 2u + sizeof(u64) * 2u + entry.payloadBytes.size();
            if(AddBinaryStringReserveBytes(entryBytes, virtualPathText))
                (void)AddBinaryReserveBytes(reserveBytes, entryBytes);
        }
        outBytes.reserve(reserveBytes);
    }

    const BuiltAssetFileHeader placeholderHeader{};
    AppendPOD(outBytes, placeholderHeader);
    if(!AppendString(outBytes, sourcePathText))
        return false;

    for(const BuiltAssetEncodeEntry& entry : entries){
        if(!entry.virtualPath)
            return false;
        if(!entry.payloadBytes.empty() && entry.payloadBytes.data() == nullptr)
            return false;

        AppendPOD(outBytes, entry.nwbSubIndex);
        AppendPOD(outBytes, entry.assetTypeHash);
        AppendPOD(outBytes, entry.virtualPath.hash());
        if(!AppendString(outBytes, entry.virtualPath.resolvedText()))
            return false;
        const u64 payloadSize = static_cast<u64>(entry.payloadBytes.size());
        AppendPOD(outBytes, payloadSize);
        AppendPOD(outBytes, entry.payloadHashFnv64);
        if(!CanAppendBytes(outBytes, entry.payloadBytes.size()))
            return false;
        AppendBytesUnchecked(outBytes, entry.payloadBytes.data(), entry.payloadBytes.size());
    }

    constexpr usize s_PayloadSectionOffset = sizeof(BuiltAssetFileHeader);
    const usize payloadSectionSize = outBytes.size() - s_PayloadSectionOffset;

    BuiltAssetFileHeader header;
    header.flags = flags;
    header.entryCount = static_cast<u32>(entries.size());
    header.paramSha256 = paramSha256;
    header.payloadSectionSize = static_cast<u64>(payloadSectionSize);
    header.payloadSectionHashFnv64 = ComputeFnv64Bytes(outBytes.data() + s_PayloadSectionOffset, payloadSectionSize);
    FOUNDATION_MEMCPY(outBytes.data(), sizeof(header), &header, sizeof(header));
    return true;
}

b8 DecodeBuiltAssetBytes(
    const Span<const u8> bytes,
    DecodedBuiltAssetFileView& outFile,
    Vector<DecodedBuiltAssetEntryView, ScratchArena>& outEntries
){
    outEntries.clear();
    outFile = {};

    usize cursor = 0u;
    const auto header = ReadPOD<BuiltAssetFileHeader>(bytes, cursor);
    if(!header)
        return false;
    if(header->magic != s_BuiltAssetMagic || header->version != s_BuiltAssetVersion || header->headerSize != sizeof(BuiltAssetFileHeader))
        return false;
    if(header->entryCount == 0u || header->payloadSectionSize > static_cast<u64>(Limit<usize>::s_Max))
        return false;

    const usize payloadSectionSize = static_cast<usize>(header->payloadSectionSize);
    if(cursor > bytes.size() || bytes.size() - cursor != payloadSectionSize)
        return false;
    if(ComputeFnv64Bytes(bytes.data() + cursor, payloadSectionSize) != header->payloadSectionHashFnv64)
        return false;

    const auto sourcePathText = ReadString(bytes, cursor);
    if(!sourcePathText)
        return false;

    constexpr usize s_MinimumEntryBytes = sizeof(u32) + sizeof(NameHash) * 2u + sizeof(u32) + sizeof(u64) * 2u;
    if(header->entryCount > (bytes.size() - cursor) / s_MinimumEntryBytes)
        return false;

    outEntries.reserve(header->entryCount);
    for(u32 entryIndex = 0u; entryIndex < header->entryCount; ++entryIndex){
        const auto nwbSubIndex = ReadPOD<u32>(bytes, cursor);
        const auto assetTypeHash = ReadPOD<NameHash>(bytes, cursor);
        const auto virtualPathHash = ReadPOD<NameHash>(bytes, cursor);
        const auto virtualPathText = ReadString(bytes, cursor);
        const auto payloadSize = ReadPOD<u64>(bytes, cursor);
        const auto payloadHashFnv64 = ReadPOD<u64>(bytes, cursor);
        if(!nwbSubIndex || !assetTypeHash || !virtualPathHash || !virtualPathText || !payloadSize || !payloadHashFnv64)
            return false;
        if(*payloadSize > static_cast<u64>(Limit<usize>::s_Max))
            return false;

        const usize payloadByteCount = static_cast<usize>(*payloadSize);
        if(!CanReadBytes(bytes, cursor, payloadByteCount))
            return false;
        const usize payloadOffset = cursor;
        const u8* const payloadData = bytes.data() + cursor;
        cursor += payloadByteCount;
        if(ComputeFnv64Bytes(payloadData, payloadByteCount) != *payloadHashFnv64)
            return false;

        Name virtualPath(*virtualPathHash);
        if(!virtualPathText->empty()){
            const auto parsedVirtualPath = Name::FromText(*virtualPathText);
            if(parsedVirtualPath && parsedVirtualPath->identityHash() == *virtualPathHash)
                virtualPath = *parsedVirtualPath;
        }
        if(!virtualPath)
            return false;

        outEntries.push_back(DecodedBuiltAssetEntryView{
            .nwbSubIndex = *nwbSubIndex,
            .assetType = Name(*assetTypeHash),
            .virtualPath = virtualPath,
            .payloadOffset = payloadOffset,
            .payloadSize = payloadByteCount,
        });
    }
    if(cursor != bytes.size())
        return false;

    outFile.header = *header;
    outFile.sourcePathText = *sourcePathText;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

