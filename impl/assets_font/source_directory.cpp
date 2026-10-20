// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "source_directory.h"

#include <global/binary_big_endian.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_font_source_directory{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr usize s_TableRecordBytes = 16u;
static constexpr usize s_HeaderTableCountFieldOffset = 4u;
static constexpr usize s_RecordOffsetFieldOffset = 8u;
static constexpr usize s_RecordLengthFieldOffset = 12u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<FontSfntDirectory> FontSfntDirectory::Read(const Span<const u8> bytes)noexcept{
    if(bytes.size() < s_FontSfntHeaderBytes)
        return MakeUnexpected(Failure{});
    const u16 tableCount = ReadU16BigEndian(NotNull<const u8*>(bytes.data() + __hidden_font_source_directory::s_HeaderTableCountFieldOffset));
    if(tableCount == 0u || tableCount > s_FontMaxTableCount || static_cast<usize>(tableCount) * __hidden_font_source_directory::s_TableRecordBytes > bytes.size() - s_FontSfntHeaderBytes)
        return MakeUnexpected(Failure{});
    return FontSfntDirectory(bytes, tableCount);
}

FontSfntDirectory::FontSfntDirectory(const Span<const u8> bytes, const u16 tableCount)noexcept
    : m_bytes(bytes)
    , m_tableCount(tableCount)
{}

u32 FontSfntDirectory::signature()const noexcept{
    return ReadU32BigEndian(NotNull<const u8*>(m_bytes.data()));
}

usize FontSfntDirectory::directoryBytes()const noexcept{
    return s_FontSfntHeaderBytes + static_cast<usize>(m_tableCount) * __hidden_font_source_directory::s_TableRecordBytes;
}

Expected<FontSfntTable> FontSfntDirectory::table(const usize index)const noexcept{
    const auto recordResult = record(index);
    if(!recordResult)
        return MakeUnexpected(Failure{});
    const u8* data = recordResult->data();
    const u32 offset = ReadU32BigEndian(NotNull<const u8*>(data + __hidden_font_source_directory::s_RecordOffsetFieldOffset));
    const u32 length = ReadU32BigEndian(NotNull<const u8*>(data + __hidden_font_source_directory::s_RecordLengthFieldOffset));
    if(offset > m_bytes.size() || length > m_bytes.size() - offset)
        return MakeUnexpected(Failure{});
    return FontSfntTable{ ReadU32BigEndian(NotNull<const u8*>(data)), offset, { m_bytes.data() + offset, length } };
}

Expected<u32> FontSfntDirectory::tableTag(const usize index)const noexcept{
    const auto recordResult = record(index);
    if(!recordResult)
        return MakeUnexpected(Failure{});
    return ReadU32BigEndian(NotNull<const u8*>(recordResult->data()));
}

Expected<Span<const u8>> FontSfntDirectory::record(const usize index)const noexcept{
    if(index >= m_tableCount)
        return MakeUnexpected(Failure{});
    return Span<const u8>{ m_bytes.data() + s_FontSfntHeaderBytes + index * __hidden_font_source_directory::s_TableRecordBytes, __hidden_font_source_directory::s_TableRecordBytes };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

