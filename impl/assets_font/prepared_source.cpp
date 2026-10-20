// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "prepared_source.h"
#include "asset.h"

#include <global/binary_little_endian.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_prepared_source{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr u32 s_Magic = 0x324e4f46u; // FON2 in little-endian wire order.
static constexpr u32 s_Version = 1u;
static constexpr usize s_HeaderBytes = 56u;
static constexpr usize s_GroupHeaderBytes = 48u;
static constexpr u32 s_MaxGroupCount = 8u;
static constexpr u32 s_MaxSide = 2048u;
static constexpr u32 s_MaxChannelCount = 4u;
static constexpr usize s_HeaderVersionOffset = 4u;
static constexpr usize s_HeaderFaceIndexOffset = 8u;
static constexpr usize s_HeaderFontByteCountOffset = 12u;
static constexpr usize s_HeaderGroupCountOffset = 16u;
static constexpr usize s_HeaderFlagsOffset = 20u;
static constexpr usize s_HeaderFontSha256Offset = 24u;
static constexpr usize s_GroupHeightOffset = 4u;
static constexpr usize s_GroupChannelCountOffset = 8u;
static constexpr usize s_GroupByteCountOffset = 12u;
static constexpr usize s_GroupSha256Offset = 16u;
static constexpr u64 s_MaxPixelBytes = 128u * 1024u * 1024u;
static constexpr u64 s_MaxFileBytes = s_HeaderBytes + s_MaxGroupCount * s_GroupHeaderBytes + s_FontMaxSourceBytes + s_MaxPixelBytes;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<u32> ImageByteCount(const u32 width, const u32 height, const u32 channels)noexcept{
    if(width == 0u || width > s_MaxSide || height == 0u || height > s_MaxSide || channels == 0u || channels > s_MaxChannelCount)
        return MakeUnexpected(Failure{});
    return width * height * channels;
}

[[nodiscard]] static bool ReadExact(GlobalFilesystemDetail::InputFileStream& stream, void* destination, const usize byteCount){
    stream.read(reinterpret_cast<char*>(destination), static_cast<GlobalFilesystemDetail::StreamSize>(byteCount));
    return stream.gcount() == static_cast<GlobalFilesystemDetail::StreamSize>(byteCount) && !stream.bad();
}

[[nodiscard]] static Unexpected<AStringView> ReadFailure(const Path& path, const AStringView reason){
    NWB_LOGGER_ERROR(NWB_TEXT("Prepared font '{}': {}"), PathToString<tchar>(path), StringConvert(reason));
    return MakeUnexpected(reason);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Core::Assets::AssetBytes, AStringView> SerializePreparedFontSource(const BinaryByteView sfnt, const u32 faceIndex, const PreparedFontImageView* groups, const usize groupCount, Core::Assets::AssetArena& arena){
    using namespace __hidden_prepared_source;
    if(!sfnt.data() || sfnt.empty() || sfnt.size() > s_FontMaxSourceBytes || faceIndex != 0u || !groups || groupCount == 0u || groupCount > s_MaxGroupCount){
        NWB_LOGGER_ERROR(NWB_TEXT("Prepared font serialization: invalid font bytes, face index, or image group count"));
        return MakeUnexpected(AStringView("invalid font bytes, face index, or image group count"));
    }

    u64 pixelByteCount = 0u;
    for(usize index = 0u; index < groupCount; ++index){
        const PreparedFontImageView& group = groups[index];
        const auto expectedBytes = ImageByteCount(group.width, group.height, group.channelCount);
        if(!expectedBytes || !group.pixels.data() || group.pixels.size() != *expectedBytes || *expectedBytes > s_MaxPixelBytes - pixelByteCount){
            NWB_LOGGER_ERROR(NWB_TEXT("Prepared font serialization: image group {} has invalid dimensions, channels, or pixel bytes"), index);
            return MakeUnexpected(AStringView("image dimensions, channels, or pixel bytes are invalid"));
        }
        pixelByteCount += *expectedBytes;
    }

    const usize byteCount = s_HeaderBytes + groupCount * s_GroupHeaderBytes + sfnt.size() + static_cast<usize>(pixelByteCount);
    Core::Assets::AssetBytes binary(arena);
    binary.reserve(byteCount);
    AppendU32LittleEndian(binary, s_Magic);
    AppendU32LittleEndian(binary, s_Version);
    AppendU32LittleEndian(binary, faceIndex);
    AppendU32LittleEndian(binary, static_cast<u32>(sfnt.size()));
    AppendU32LittleEndian(binary, static_cast<u32>(groupCount));
    AppendU32LittleEndian(binary, 0u);
    const Sha256Digest fontHash = ComputeSha256(sfnt);
    binary.insert(binary.end(), fontHash.bytes, fontHash.bytes + sizeof(fontHash.bytes));
    for(usize index = 0u; index < groupCount; ++index){
        const PreparedFontImageView& group = groups[index];
        AppendU32LittleEndian(binary, group.width);
        AppendU32LittleEndian(binary, group.height);
        AppendU32LittleEndian(binary, group.channelCount);
        AppendU32LittleEndian(binary, static_cast<u32>(group.pixels.size()));
        const Sha256Digest imageHash = ComputeSha256(group.pixels);
        binary.insert(binary.end(), imageHash.bytes, imageHash.bytes + sizeof(imageHash.bytes));
    }
    binary.insert(binary.end(), sfnt.data(), sfnt.data() + sfnt.size());
    for(usize index = 0u; index < groupCount; ++index){
        const BinaryByteView pixels = groups[index].pixels;
        binary.insert(binary.end(), pixels.data(), pixels.data() + pixels.size());
    }
    return binary;
}

Expected<PreparedFontSource, AStringView> ReadPreparedFontSource(const Path& path, Core::Assets::AssetArena& arena, const bool includePixels){
    using namespace __hidden_prepared_source;
    GlobalFilesystemDetail::InputFileStream stream(path, GlobalFilesystemDetail::InputFileStream::binary);
    if(!stream.is_open())
        return ReadFailure(path, "could not open paired .font source");
    stream.seekg(0, GlobalFilesystemDetail::InputFileStream::end);
    const GlobalFilesystemDetail::StreamOffset fileEnd = stream.tellg();
    if(!stream || fileEnd < static_cast<GlobalFilesystemDetail::StreamOffset>(s_HeaderBytes) || static_cast<u64>(fileEnd) > s_MaxFileBytes)
        return ReadFailure(path, "file length is outside prepared-source limits");
    stream.seekg(0, GlobalFilesystemDetail::InputFileStream::beg);
    if(!stream)
        return ReadFailure(path, "could not seek prepared-source header");

    u8 header[s_HeaderBytes];
    if(!ReadExact(stream, header, sizeof(header)))
        return ReadFailure(path, "prepared-source header is truncated");
    const u32 fontByteCount = ReadU32LittleEndian(NotNull<const u8*>(header + s_HeaderFontByteCountOffset));
    const u32 groupCount = ReadU32LittleEndian(NotNull<const u8*>(header + s_HeaderGroupCountOffset));
    if(
        ReadU32LittleEndian(NotNull<const u8*>(header)) != s_Magic
        || ReadU32LittleEndian(NotNull<const u8*>(header + s_HeaderVersionOffset)) != s_Version
        || ReadU32LittleEndian(NotNull<const u8*>(header + s_HeaderFaceIndexOffset)) != 0u
        || ReadU32LittleEndian(NotNull<const u8*>(header + s_HeaderFlagsOffset)) != 0u
    )
        return ReadFailure(path, "unsupported prepared-source magic, version, face index, or flags; rebuild required");
    if(fontByteCount == 0u || fontByteCount > s_FontMaxSourceBytes || groupCount == 0u || groupCount > s_MaxGroupCount)
        return ReadFailure(path, "font size or image group count is outside prepared-source limits");

    PreparedFontSource candidate(arena);
    candidate.faceIndex = ReadU32LittleEndian(NotNull<const u8*>(header + s_HeaderFaceIndexOffset));
    NWB_MEMCPY(candidate.fontSha256.bytes, sizeof(candidate.fontSha256.bytes), header + s_HeaderFontSha256Offset, sizeof(candidate.fontSha256.bytes));
    candidate.groups.reserve(groupCount);
    u32 groupByteCounts[s_MaxGroupCount];
    u64 pixelByteCount = 0u;
    for(u32 index = 0u; index < groupCount; ++index){
        u8 groupHeader[s_GroupHeaderBytes];
        if(!ReadExact(stream, groupHeader, sizeof(groupHeader)))
            return ReadFailure(path, "image directory is truncated");
        PreparedFontImageGroup group(candidate.fontBytes.get_allocator().arena());
        group.width = ReadU32LittleEndian(NotNull<const u8*>(groupHeader));
        group.height = ReadU32LittleEndian(NotNull<const u8*>(groupHeader + s_GroupHeightOffset));
        group.channelCount = ReadU32LittleEndian(NotNull<const u8*>(groupHeader + s_GroupChannelCountOffset));
        const auto expectedBytes = ImageByteCount(group.width, group.height, group.channelCount);
        if(!expectedBytes || ReadU32LittleEndian(NotNull<const u8*>(groupHeader + s_GroupByteCountOffset)) != *expectedBytes || *expectedBytes > s_MaxPixelBytes - pixelByteCount)
            return ReadFailure(path, "image dimensions, channels, or byte count are invalid");
        pixelByteCount += *expectedBytes;
        groupByteCounts[index] = *expectedBytes;
        NWB_MEMCPY(group.sha256.bytes, sizeof(group.sha256.bytes), groupHeader + s_GroupSha256Offset, sizeof(group.sha256.bytes));
        candidate.groups.push_back(Move(group));
    }

    const u64 expectedFileBytes = s_HeaderBytes + static_cast<u64>(groupCount) * s_GroupHeaderBytes + fontByteCount + pixelByteCount;
    if(expectedFileBytes != static_cast<u64>(fileEnd))
        return ReadFailure(path, "prepared-source layout is truncated or has trailing bytes");
    candidate.fontBytes.resize(fontByteCount);
    if(!ReadExact(stream, candidate.fontBytes.data(), candidate.fontBytes.size()))
        return ReadFailure(path, "prepared SFNT bytes are truncated");
    if(ComputeSha256({ candidate.fontBytes.data(), candidate.fontBytes.size() }) != candidate.fontSha256)
        return ReadFailure(path, "prepared SFNT content hash differs from its directory");

    if(includePixels){
        for(u32 index = 0u; index < groupCount; ++index){
            PreparedFontImageGroup& group = candidate.groups[index];
            group.pixels.resize(groupByteCounts[index]);
            if(!ReadExact(stream, group.pixels.data(), group.pixels.size()))
                return ReadFailure(path, "prepared image bytes are truncated");
            if(ComputeSha256({ group.pixels.data(), group.pixels.size() }) != group.sha256)
                return ReadFailure(path, "prepared image content hash differs from its directory");
        }
    }

    // Recheck the opened stream length even when its image payloads were skipped.
    stream.seekg(0, GlobalFilesystemDetail::InputFileStream::end);
    if(!stream || stream.tellg() != fileEnd)
        return ReadFailure(path, "prepared source changed length during read");
    char extraByte = 0;
    stream.read(&extraByte, 1);
    if(stream.gcount() != 0 || !stream.eof() || stream.bad())
        return ReadFailure(path, "prepared source changed during read");
    return candidate;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

