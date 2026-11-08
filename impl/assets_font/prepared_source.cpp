// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "prepared_source.h"

#include <impl/assets_font/prepared_font_binary_codec.adhd.h>

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
    const Expected<u64, AStringView> expectedPayloadBytes = PreparedFontBinaryCodec{}.validateSerializeInputs(sfnt.size(), sfnt.data() != nullptr, faceIndex, groups);
    if(!expectedPayloadBytes)
        return MakeUnexpected(expectedPayloadBytes.error());
    Core::Assets::AssetBytes binary(arena);
    PreparedFontBinaryCodec{}.appendSourceBinary(Span<const u8>(sfnt.data(), sfnt.size()), faceIndex, groups, *expectedPayloadBytes, binary);
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
    u32 faceIndex = 0u;
    u32 fontByteCount = 0u;
    u32 groupCount = 0u;
    const Expected<b8, AStringView> headerDecode = PreparedFontBinaryCodec{}.decodeHeader(header, faceIndex, fontByteCount, groupCount);
    if(!headerDecode)
        return ReadFailure(path, headerDecode.error());

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
        const Expected<b8, AStringView> groupDecode = PreparedFontBinaryCodec{}.decodeGroupHeader(
            groupHeader,
            pixelByteCount,
            group.width,
            group.height,
            group.channelCount,
            groupByteCounts[index]
        );
        if(!groupDecode)
            return ReadFailure(path, groupDecode.error());
        NWB_MEMCPY(group.sha256.bytes, sizeof(group.sha256.bytes), groupHeader.data() + s_PreparedFontGroupSha256Offset, sizeof(group.sha256.bytes));
        candidate.groups.push_back(Move(group));
    }

    const Expected<u64> expectedFileBytes = PreparedFontBinaryCodec{}.expectedPayloadBytes(fontByteCount, groupCount, pixelByteCount);
    if(!expectedFileBytes || *expectedFileBytes != static_cast<u64>(fileEnd))
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

