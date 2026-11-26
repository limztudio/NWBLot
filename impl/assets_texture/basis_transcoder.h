// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/assets_texture/basis_transcode_types.adhd.h>
#include <global/array.h>
#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace TextureBasis{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void InitializeBasisTranscoder();
[[nodiscard]] b8 TranscodeBasisAstcSlice(Span<const u8> source, Span<u8> destination, usize outputOffset);
[[nodiscard]] b8 TranscodeBasisAstcSliceRange(Span<const u8> source, Span<u8> destination, usize outputOffset, usize blockBegin, usize blockEnd);
[[nodiscard]] b8 UnpackBasisRgbaSlice(u32 width, u32 height, u32 blockCountX, u32 blockCountY, Span<const u8> source, b8 alphaOnly, Span<u8> destination, usize outputOffset);
[[nodiscard]] b8 UnpackBasisRgbaSliceRange(u32 width, u32 height, u32 blockCountX, u32 blockCountY, Span<const u8> source, b8 alphaOnly, Span<u8> destination, usize outputOffset, u32 blockRowBegin, u32 blockRowEnd);
[[nodiscard]] b8 TranscodeBasisImage(Impl::BasisTranscodeFormat::Enum format, const Impl::BasisTranscodeLayout& layout, Span<const u8> source, Span<u8> bytes, usize offset, usize count);
[[nodiscard]] b8 TranscodeBasisImageRowRange(Impl::BasisTranscodeFormat::Enum format, const Impl::BasisTranscodeLayout& layout, Span<const u8> source, Span<u8> bytes, usize offset, usize count, u32 blockRowBegin, u32 blockRowEnd);
[[nodiscard]] u16 BasisFloatToHalf(f32 value)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

