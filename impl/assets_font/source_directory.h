// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "asset.h"

#include <global/expected.h>
#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_FontSfntHeaderBytes = 12u;
inline constexpr u32 s_FontSfntHeadTag = 0x68656164u;
inline constexpr u32 s_FontSfntMaxpTag = 0x6d617870u;

struct FontSfntTable{
    u32 tag;
    u32 offset;
    Span<const u8> bytes;
};

// Borrows the source buffer; the caller keeps it alive and unchanged while reading the directory or its tables.
class FontSfntDirectory final{
public:
    [[nodiscard]] static Expected<FontSfntDirectory> Read(Span<const u8> bytes)noexcept;


private:
    FontSfntDirectory(Span<const u8> bytes, u16 tableCount)noexcept;


public:
    [[nodiscard]] u32 signature()const noexcept;
    [[nodiscard]] usize directoryBytes()const noexcept;
    [[nodiscard]] u16 tableCount()const noexcept{ return m_tableCount; }
    [[nodiscard]] Expected<FontSfntTable> table(usize index)const noexcept;
    [[nodiscard]] Expected<u32> tableTag(usize index)const noexcept;


private:
    [[nodiscard]] Expected<Span<const u8>> record(usize index)const noexcept;


private:
    Span<const u8> m_bytes;
    u16 m_tableCount;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<u16> ReadFontSfntU16(Span<const u8> bytes, usize offset)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

