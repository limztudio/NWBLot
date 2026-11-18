// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/global.h>
#include <global/array.h>
#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace BasisTranscodeFormat{
    enum Enum : u8{
        Bc7,
        Bc6h,
        AstcHdr4x4,
        RgbaHalf,
    };
};

struct BasisTranscodeLayout{
    u32 width = 0u;
    u32 height = 0u;
    u32 blockCountX = 0u;
    u32 blockCountY = 0u;
    u32 mipLevel = 0u;
    u32 outputElements = 0u;
    u32 outputRowPitch = 0u;
};

void InitializeBasisTranscoder();
[[nodiscard]] b8 TranscodeBasisAstcBlock(Span<const u8> source, usize offset, Span<u8> destination, usize outputOffset);
[[nodiscard]] b8 UnpackBasisRgbaBlock(Span<const u8> source, usize offset, Array<u8, 64u>& rgba);
[[nodiscard]] b8 TranscodeBasisImage(BasisTranscodeFormat::Enum format, const BasisTranscodeLayout& layout, Span<const u8> source, Span<u8> bytes, usize offset, usize count);
[[nodiscard]] u16 BasisFloatToHalf(f32 value)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

