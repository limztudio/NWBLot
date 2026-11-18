// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "basis_transcoder.h"

#include <basisu_transcoder.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace TextureBasis{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void InitializeBasisTranscoder(){
    static const b8 initialized = [](){
        basist::basisu_transcoder_init();
        return true;
    }();
    (void)initialized;
}

b8 TranscodeBasisAstcBlock(const Span<const u8> source, const usize offset, const Span<u8> destination, const usize outputOffset){
    basist::uastc_block block;
    if(offset > source.size() || sizeof(block) > source.size() - offset || outputOffset > destination.size() || sizeof(block) > destination.size() - outputOffset)
        return false;
    FOUNDATION_MEMCPY(&block, sizeof(block), source.data() + offset, sizeof(block));
    return basist::transcode_uastc_to_astc(block, destination.data() + outputOffset);
}

b8 UnpackBasisRgbaBlock(const Span<const u8> source, const usize offset, Array<u8, 64u>& rgba){
    basist::uastc_block block;
    if(offset > source.size() || sizeof(block) > source.size() - offset)
        return false;
    FOUNDATION_MEMCPY(&block, sizeof(block), source.data() + offset, sizeof(block));
    Array<basist::color32, 16u> colors;
    if(!basist::unpack_uastc(block, colors.data(), false))
        return false;
    static_assert(sizeof(colors) == sizeof(rgba) && offsetof(basist::color32, r) == 0 && offsetof(basist::color32, g) == 1 && offsetof(basist::color32, b) == 2 && offsetof(basist::color32, a) == 3);
    FOUNDATION_MEMCPY(rgba.data(), rgba.size(), colors.data(), rgba.size());
    return true;
}

b8 TranscodeBasisImage(const Impl::BasisTranscodeFormat::Enum format, const Impl::BasisTranscodeLayout& layout, const Span<const u8> source, const Span<u8> bytes, const usize offset, const usize count){
    if(offset > bytes.size() || count > bytes.size() - offset)
        return false;
    const Span<u8> destination = bytes.subspan(offset, count);
    using Format = basist::transcoder_texture_format;
    Format target;
    switch(format){
    case Impl::BasisTranscodeFormat::Bc7: target = Format::cTFBC7_RGBA; break;
    case Impl::BasisTranscodeFormat::Bc6h: target = Format::cTFBC6H; break;
    case Impl::BasisTranscodeFormat::AstcHdr4x4: target = Format::cTFASTC_HDR_4x4_RGBA; break;
    case Impl::BasisTranscodeFormat::RgbaHalf: target = Format::cTFRGBA_HALF; break;
    default: return false;
    }
    if(!layout.width || !layout.height || layout.blockCountX != 1u + (layout.width - 1u) / 4u || layout.blockCountY != 1u + (layout.height - 1u) / 4u)
        return false;
    const u64 blocks = static_cast<u64>(layout.blockCountX) * layout.blockCountY;
    const u32 width = format == Impl::BasisTranscodeFormat::RgbaHalf ? layout.width : layout.blockCountX;
    const u32 rows = format == Impl::BasisTranscodeFormat::RgbaHalf ? layout.height : layout.blockCountY;
    if(source.size() > Limit<u32>::s_Max || blocks > Limit<u32>::s_Max / sizeof(basist::uastc_block) || blocks * sizeof(basist::uastc_block) != source.size()
        || layout.outputRowPitch < width || static_cast<u64>(layout.outputRowPitch) * rows > layout.outputElements
        || static_cast<u64>(layout.outputElements) * basist::basis_get_bytes_per_block_or_pixel(target) > destination.size())
        return false;
    const auto transcode = [&](auto& decoder, const b8 alpha){
        return decoder.transcode_image(target, destination.data(), layout.outputElements, source.data(), static_cast<u32>(source.size()),
            layout.blockCountX, layout.blockCountY, layout.width, layout.height, layout.mipLevel, 0u, static_cast<u32>(source.size()),
            0u, alpha, false, layout.outputRowPitch, nullptr, layout.height);
    };
    if(format == Impl::BasisTranscodeFormat::Bc7){
        basist::basisu_lowlevel_uastc_ldr_4x4_transcoder decoder;
        return transcode(decoder, true);
    }
    basist::basisu_lowlevel_uastc_hdr_4x4_transcoder decoder;
    return transcode(decoder, false);
}

u16 BasisFloatToHalf(const f32 value)noexcept{
    static_assert(sizeof(basist::half_float) == sizeof(u16));
    return basist::float_to_half(value);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

