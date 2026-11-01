// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "prepared_source.h"

#include <global/binary_little_endian.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_prepared_source{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Unexpected<AStringView> ReadFailure(const Path& path, const AStringView reason){
    NWB_LOGGER_ERROR(NWB_TEXT("Prepared font '{}': {}"), PathToString<tchar>(path), ConvertString(reason));
    return MakeUnexpected(reason);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Core::Assets::AssetBytes, AStringView> SerializePreparedFontSource(const BinaryByteView sfnt, const u32 faceIndex, const Span<const PreparedFontImageView> groups, Core::Assets::AssetArena& arena){
    if(!sfnt.data() || !IsValidPreparedFontHeaderInputs(sfnt.size(), faceIndex, groups.size())){
        NWB_LOGGER_ERROR(NWB_TEXT("Prepared font serialization: invalid font bytes, face index, or image group count"));
        return MakeUnexpected(s_PreparedFontInvalidSerializeInputReason);
    }

    u64 pixelByteCount = 0u;
    for(usize index = 0u; index < groups.size(); ++index){
        const PreparedFontImageView& group = groups[index];
        u32 expectedBytes = 0u;
        if(!TryComputePreparedFontImageByteCount(group.width, group.height, group.channelCount, expectedBytes) || !group.pixels.data() || group.pixels.size() != expectedBytes || expectedBytes > s_PreparedFontMaxPixelBytes - pixelByteCount){
            NWB_LOGGER_ERROR(NWB_TEXT("Prepared font serialization: image group {} has invalid dimensions, channels, or pixel bytes"), index);
            return MakeUnexpected(s_PreparedFontInvalidSerializeGroupReason);
        }
        pixelByteCount += expectedBytes;
    }

    u64 expectedPayloadBytes = 0u;
    if(!TryComputePreparedFontExpectedPayloadBytes(sfnt.size(), groups.size(), pixelByteCount, expectedPayloadBytes)){
        NWB_LOGGER_ERROR(NWB_TEXT("Prepared font serialization: invalid font bytes, face index, or image group count"));
        return MakeUnexpected(s_PreparedFontInvalidSerializeInputReason);
    }
    Core::Assets::AssetBytes binary(arena);
    binary.reserve(static_cast<usize>(expectedPayloadBytes));
    AppendU32LittleEndian(binary, s_PreparedFontMagic);
    AppendU32LittleEndian(binary, s_PreparedFontVersion);
    AppendU32LittleEndian(binary, faceIndex);
    AppendU32LittleEndian(binary, static_cast<u32>(sfnt.size()));
    AppendU32LittleEndian(binary, static_cast<u32>(groups.size()));
    AppendU32LittleEndian(binary, 0u);
    const Sha256Digest fontHash = ComputeSha256(sfnt);
    binary.insert(binary.end(), fontHash.bytes, fontHash.bytes + sizeof(fontHash.bytes));
    for(usize index = 0u; index < groups.size(); ++index){
        const PreparedFontImageView& group = groups[index];
        AppendU32LittleEndian(binary, group.width);
        AppendU32LittleEndian(binary, group.height);
        AppendU32LittleEndian(binary, group.channelCount);
        AppendU32LittleEndian(binary, static_cast<u32>(group.pixels.size()));
        const Sha256Digest imageHash = ComputeSha256({ group.pixels.data(), group.pixels.size() });
        binary.insert(binary.end(), imageHash.bytes, imageHash.bytes + sizeof(imageHash.bytes));
    }
    binary.insert(binary.end(), sfnt.data(), sfnt.data() + sfnt.size());
    for(usize index = 0u; index < groups.size(); ++index){
        const Span<const u8> pixels = groups[index].pixels;
        binary.insert(binary.end(), pixels.data(), pixels.data() + pixels.size());
    }
    return binary;
}

Expected<PreparedFontSource, AStringView> ReadPreparedFontSource(const Path& path, const b8 includePixels, Core::Assets::AssetArena& arena){
    using namespace __hidden_prepared_source;
    InputFileStream stream(path, InputFileStream::binary);
    if(!stream.is_open())
        return ReadFailure(path, s_PreparedFontOpenFailedReason);
    stream.seekg(0, InputFileStream::end);
    const StreamOffset fileEnd = stream.tellg();
    if(!stream || fileEnd < static_cast<StreamOffset>(s_PreparedFontHeaderBytes) || static_cast<u64>(fileEnd) > s_PreparedFontMaxFileBytes)
        return ReadFailure(path, s_PreparedFontLengthOutOfBoundsReason);
    stream.seekg(0, InputFileStream::beg);
    if(!stream)
        return ReadFailure(path, s_PreparedFontSeekFailedReason);

    Array<u8, s_PreparedFontHeaderBytes> header{};
    if(!ReadStreamExact(stream, NotNull<void*>(header.data()), sizeof(header)))
        return ReadFailure(path, s_PreparedFontHeaderTruncatedReason);
    const u32 magic = ReadU32LittleEndian(NotNull<const u8*>(header.data()));
    const u32 version = ReadU32LittleEndian(NotNull<const u8*>(header.data() + s_PreparedFontHeaderVersionOffset));
    const u32 faceIndex = ReadU32LittleEndian(NotNull<const u8*>(header.data() + s_PreparedFontHeaderFaceIndexOffset));
    const u32 fontByteCount = ReadU32LittleEndian(NotNull<const u8*>(header.data() + s_PreparedFontHeaderFontByteCountOffset));
    const u32 groupCount = ReadU32LittleEndian(NotNull<const u8*>(header.data() + s_PreparedFontHeaderGroupCountOffset));
    const u32 flags = ReadU32LittleEndian(NotNull<const u8*>(header.data() + s_PreparedFontHeaderFlagsOffset));
    if(!IsValidPreparedFontHeader(magic, version, faceIndex, flags))
        return ReadFailure(path, s_PreparedFontUnsupportedHeaderReason);
    if(!IsValidPreparedFontHeaderInputs(fontByteCount, faceIndex, groupCount))
        return ReadFailure(path, s_PreparedFontCountsOutOfBoundsReason);

    PreparedFontSource candidate(arena);
    candidate.faceIndex = faceIndex;
    NWB_MEMCPY(candidate.fontSha256.bytes, sizeof(candidate.fontSha256.bytes), header.data() + s_PreparedFontHeaderFontSha256Offset, sizeof(candidate.fontSha256.bytes));
    candidate.groups.reserve(groupCount);
    Array<u32, s_PreparedFontMaxGroupCount> groupByteCounts{};
    u64 pixelByteCount = 0u;
    for(u32 index = 0u; index < groupCount; ++index){
        Array<u8, s_PreparedFontGroupHeaderBytes> groupHeader{};
        if(!ReadStreamExact(stream, NotNull<void*>(groupHeader.data()), sizeof(groupHeader)))
            return ReadFailure(path, s_PreparedFontImageDirectoryTruncatedReason);
        PreparedFontImageGroup group(candidate.fontBytes.get_allocator().arena());
        group.width = ReadU32LittleEndian(NotNull<const u8*>(groupHeader.data()));
        group.height = ReadU32LittleEndian(NotNull<const u8*>(groupHeader.data() + s_PreparedFontGroupHeightOffset));
        group.channelCount = ReadU32LittleEndian(NotNull<const u8*>(groupHeader.data() + s_PreparedFontGroupChannelCountOffset));
        u32 expectedBytes = 0u;
        if(
            !TryComputePreparedFontImageByteCount(group.width, group.height, group.channelCount, expectedBytes)
            || ReadU32LittleEndian(NotNull<const u8*>(groupHeader.data() + s_PreparedFontGroupByteCountOffset)) != expectedBytes
            || expectedBytes > s_PreparedFontMaxPixelBytes - pixelByteCount
        )
            return ReadFailure(path, s_PreparedFontInvalidImageGroupReason);
        pixelByteCount += expectedBytes;
        groupByteCounts[index] = expectedBytes;
        NWB_MEMCPY(group.sha256.bytes, sizeof(group.sha256.bytes), groupHeader.data() + s_PreparedFontGroupSha256Offset, sizeof(group.sha256.bytes));
        candidate.groups.push_back(Move(group));
    }

    u64 expectedFileBytes = 0u;
    if(!TryComputePreparedFontExpectedPayloadBytes(fontByteCount, groupCount, pixelByteCount, expectedFileBytes) || expectedFileBytes != static_cast<u64>(fileEnd))
        return ReadFailure(path, s_PreparedFontLayoutMismatchReason);
    candidate.fontBytes.resize(fontByteCount);
    if(!ReadStreamExact(stream, NotNull<void*>(candidate.fontBytes.data()), candidate.fontBytes.size()))
        return ReadFailure(path, s_PreparedFontSfntBytesTruncatedReason);
    if(ComputeSha256({ candidate.fontBytes.data(), candidate.fontBytes.size() }) != candidate.fontSha256)
        return ReadFailure(path, s_PreparedFontSfntHashMismatchReason);

    if(includePixels){
        for(u32 index = 0u; index < groupCount; ++index){
            PreparedFontImageGroup& group = candidate.groups[index];
            group.pixels.resize(groupByteCounts[index]);
            if(!ReadStreamExact(stream, NotNull<void*>(group.pixels.data()), group.pixels.size()))
                return ReadFailure(path, s_PreparedFontImageBytesTruncatedReason);
            if(ComputeSha256({ group.pixels.data(), group.pixels.size() }) != group.sha256)
                return ReadFailure(path, s_PreparedFontImageHashMismatchReason);
        }
    }

    // Recheck the opened stream length even when its image payloads were skipped.
    stream.seekg(0, InputFileStream::end);
    if(!stream || stream.tellg() != fileEnd)
        return ReadFailure(path, s_PreparedFontLengthChangedReason);
    if(!VerifyStreamAtEnd(stream))
        return ReadFailure(path, s_PreparedFontChangedDuringReadReason);
    return candidate;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

