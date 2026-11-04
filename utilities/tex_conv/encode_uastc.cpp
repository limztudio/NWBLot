// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "encode.h"

#include <core/common/log.h>

#include <global/simdmath.h>

#include <basisu_comp.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TEX_CONV_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_encode_uastc{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using FloatMipChain = basisu::vector<FloatPlanes>;
using EncodedMip = TextureFormat::MipLevelLayout;

struct AlphaTransport{
    TextureAlphaMode::Enum mode = TextureAlphaMode::Opaque;
    u8 constantUnorm8 = TextureFormat::s_OpaqueAlphaUnorm8;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<::Vector<EncodedMip, Core::Alloc::ScratchArena>> BuildMipLevels(const TextureEncodeInput& entry, Core::Alloc::ScratchArena& scratchArena){
    auto mipLevels = TextureFormat::BuildMipChainLayout(
        entry.format,
        entry.dimension,
        entry.width,
        entry.height,
        entry.depth,
        ::Vector<EncodedMip, Core::Alloc::ScratchArena>(scratchArena),
        [](const TextureFormat::MipLevelLayout& level)noexcept -> EncodedMip{
            return level;
        }
    );
    if(!mipLevels){
        switch(mipLevels.error().failure){
        case TextureFormat::MipChainLayoutFailure::InvalidMipChain:
            NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: dimensions cannot form a complete mip chain."));
            break;
        case TextureFormat::MipChainLayoutFailure::BlockLayoutOverflow:
            NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: mip {} block grid exceeds runtime limits."), mipLevels.error().mipIndex);
            break;
        case TextureFormat::MipChainLayoutFailure::SliceCountInvalid:
            NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: mip {} has an invalid slice count."), mipLevels.error().mipIndex);
            break;
        case TextureFormat::MipChainLayoutFailure::ByteSizeOverflow:
            NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: mip {} byte size overflows."), mipLevels.error().mipIndex);
            break;
        case TextureFormat::MipChainLayoutFailure::OffsetOverflow:
        default:
            NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: mip payload offsets overflow."));
            break;
        }
        return MakeUnexpected(Failure{});
    }
    return Move(*mipLevels);
}

[[nodiscard]] static b8 InitializeEncoder(){
    // All mip encoders in this process share the initialized Basis tables.
    static const b8 s_Initialized = basisu::basisu_encoder_init(false);
    if(!s_Initialized)
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: failed to initialize Basis Universal."));
    return s_Initialized;
}

[[nodiscard]] static Expected<FloatPlanes> GenerateNextMip(const FloatPlanes& source, const TextureDimension::Enum dimension){
    if(source.empty())
        return MakeUnexpected(Failure{});
    const u32 sourceWidth = source.front().get_width();
    const u32 sourceHeight = source.front().get_height();
    const u32 targetWidth = TextureFormat::NextMipExtent(sourceWidth);
    const u32 targetHeight = TextureFormat::NextMipExtent(sourceHeight);
    FloatPlanes filtered;
    filtered.resize(source.size());
    for(usize planeIndex = 0u; planeIndex < source.size(); ++planeIndex){
        basisu::imagef& target = filtered[planeIndex];
        target.resize(targetWidth, targetHeight);
        if(!basisu::image_resample(source[planeIndex], target, s_MipFilter.data(), 1.0f, false, 0u, s_ColorChannelCount)){
            NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: failed to filter linear mip planes."));
            return MakeUnexpected(Failure{});
        }
    }
    if(dimension != TextureDimension::Texture3D || source.size() == 1u)
        return filtered;

    const u32 sourceDepth = static_cast<u32>(source.size());
    const SIMDVectorSingle inverseDepth = VectorDivide(s_SIMDOne, VectorReplicate(static_cast<f32>(sourceDepth)));
    const u32 targetDepth = sourceDepth >> 1u;
    FloatPlanes target;
    target.resize(targetDepth);
    for(u32 z = 0u; z < targetDepth; ++z){
        // Integer interval coordinates preserve fractional source-slab coverage when the depth is odd.
        const VolumeMipZSlabRange slabRange = ComputeVolumeMipZSlabRange(z, sourceDepth, targetDepth);
        basisu::imagef& plane = target[z];
        plane.resize(targetWidth, targetHeight);
        for(u32 y = 0u; y < targetHeight; ++y){
            for(u32 x = 0u; x < targetWidth; ++x){
                SIMDVectorSingle sum = VectorZero();
                for(u32 sourceZ = slabRange.first; sourceZ < slabRange.end; ++sourceZ){
                    const basisu::vec4F& color = filtered[sourceZ](x, y);
                    sum = AccumulateVolumeMipZSlabTexel(slabRange, sourceZ, targetDepth, VectorSet(color[0u], color[1u], color[2u], color[3u]), sum);
                }
                const SIMDVectorSingle mean = VectorMultiply(sum, inverseDepth);
                plane(x, y).set(VectorGetX(mean), VectorGetY(mean), VectorGetZ(mean), VectorGetW(mean));
            }
        }
    }
    return target;
}

[[nodiscard]] static Expected<AlphaTransport> DeriveAlpha(const FloatMipChain& mips, const TexturePayloadFormat::Enum format){
    AlphaTransport transport;
    b8 found = false;
    b8 constant = true;
    b8 opaque = true;
    u8 firstAlpha = TextureFormat::s_OpaqueAlphaUnorm8;
    for(const FloatPlanes& planes : mips){
        for(const basisu::imagef& plane : planes){
            for(u32 y = 0u; y < plane.get_height(); ++y){
                for(u32 x = 0u; x < plane.get_width(); ++x){
                    const basisu::vec4F& color = plane(x, y);
                    const auto alphaResult = TryQuantizeTexConvMipAlpha(VectorSet(color[0u], color[1u], color[2u], color[3u]));
                    if(!alphaResult)
                        return MakeUnexpected(Failure{});
                    const u8 alpha = *alphaResult;
                    if(!found){
                        firstAlpha = alpha;
                        found = true;
                    }
                    else if(alpha != firstAlpha)
                        constant = false;
                    opaque = opaque && alpha == TextureFormat::s_OpaqueAlphaUnorm8;
                }
            }
        }
    }
    if(!found)
        return MakeUnexpected(Failure{});
    if(opaque)
        transport.mode = TextureAlphaMode::Opaque;
    else if(format == TexturePayloadFormat::UastcLdr4x4)
        transport.mode = TextureAlphaMode::EmbeddedLdr;
    else if(constant){
        transport.mode = TextureAlphaMode::ConstantUnorm8;
        transport.constantUnorm8 = firstAlpha;
    }
    else
        transport.mode = TextureAlphaMode::SeparateUastcLdr4x4;
    return transport;
}

[[nodiscard]] static b8 AppendEncodedMip(const basisu::basisu_backend_output& output, const EncodedMip& mip, const basist::basis_tex_format format, Core::Alloc::ScratchArena& scratchArena, Vector<u8>& bytes){
    if(output.m_tex_format != format || output.m_slice_desc.size() != mip.sliceCount || output.m_slice_desc.size() != output.m_slice_image_data.size())
        return false;

    ::Vector<usize, Core::Alloc::ScratchArena> slices(mip.sliceCount, s_InvalidSlice, scratchArena);
    const u64 planeByteCount = mip.sizeBytes / mip.sliceCount;
    for(usize index = 0u; index < output.m_slice_desc.size(); ++index){
        const basisu::basisu_backend_slice_desc& desc = output.m_slice_desc[index];
        if(
            !IsValidTexConvEncodedSliceDesc(
                desc.m_source_file_index,
                desc.m_mip_index,
                desc.m_orig_width,
                desc.m_orig_height,
                desc.m_num_blocks_x,
                desc.m_num_blocks_y,
                output.m_slice_image_data[index].size_in_bytes(),
                mip.sliceCount,
                mip.width,
                mip.height,
                mip.blockCountX,
                mip.blockCountY,
                planeByteCount,
                desc.m_source_file_index < mip.sliceCount ? slices[desc.m_source_file_index] : 0u
            )
        )
            return false;
        slices[desc.m_source_file_index] = index;
    }
    NWB_ASSERT(mip.sizeBytes <= Limit<usize>::s_Max && bytes.size() <= Limit<usize>::s_Max - static_cast<usize>(mip.sizeBytes));
    for(const usize index : slices){
        NWB_ASSERT(index != s_InvalidSlice);
        const basisu::uint8_vec& blocks = output.m_slice_image_data[index];
        bytes.insert(bytes.end(), blocks.get_ptr(), blocks.get_ptr() + blocks.size_in_bytes());
    }
    return true;
}

[[nodiscard]] static b8 EncodeMip(const FloatPlanes& planes, const TextureDimension::Enum dimension, const TexturePayloadFormat::Enum format, const b8 alphaOnly, const EncodedMip& mip, Core::Alloc::ScratchArena& scratchArena, Vector<u8>& bytes){
    basisu::job_pool jobs(s_EncoderWorkerCount);
    basisu::basis_compressor_params params;
    const b8 hdr = !alphaOnly && IsHdrTexturePayloadFormat(format);
    const basist::basis_tex_format basisFormat = hdr ? basist::basis_tex_format::cUASTC_HDR_4x4 : basist::basis_tex_format::cUASTC_LDR_4x4;
    params.set_format_mode(basisFormat);
    params.set_srgb_options(false);
    params.m_status_output = false;
    params.m_compute_stats = false;
    params.m_print_stats = false;
    params.m_write_output_basis_or_ktx2_files = false;
    params.m_create_ktx2_file = false;
    params.m_pJob_pool = &jobs;
    params.m_read_source_images = false;
    params.m_mip_gen = false;
    params.m_tex_type = dimension == TextureDimension::TextureCube ? basist::cBASISTexTypeCubemapArray : dimension == TextureDimension::Texture3D ? basist::cBASISTexTypeVolume : basist::cBASISTexType2D;
    if(hdr){
        params.m_source_images_hdr = planes;
        for(basisu::imagef& plane : params.m_source_images_hdr){
            for(u32 y = 0u; y < plane.get_height(); ++y){
                for(u32 x = 0u; x < plane.get_width(); ++x)
                    plane(x, y)[3u] = 1.0f;
            }
        }
    }
    else{
        params.m_source_images.resize(planes.size());
        for(usize planeIndex = 0u; planeIndex < planes.size(); ++planeIndex){
            const basisu::imagef& source = planes[planeIndex];
            basisu::image& target = params.m_source_images[planeIndex];
            target.resize(mip.width, mip.height);
            for(u32 y = 0u; y < mip.height; ++y){
                for(u32 x = 0u; x < mip.width; ++x){
                    const basisu::vec4F& color = source(x, y);
                    const Array<u8, 4u> rgba = QuantizeTexConvUnormBytes(VectorSet(color[0u], color[1u], color[2u], color[3u]), alphaOnly, TextureFormat::s_OpaqueAlphaUnorm8);
                    target(x, y) = basisu::color_rgba(rgba[0u], rgba[1u], rgba[2u], rgba[3u]);
                }
            }
        }
    }

    basisu::basis_compressor compressor;
    if(!compressor.init(params)){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: Basis Universal failed to initialize a linear mip encoder."));
        return false;
    }
    const basisu::basis_compressor::error_code encoded = compressor.process();
    if(encoded != basisu::basis_compressor::cECSuccess){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: Basis Universal failed to encode a linear mip, error {}."), static_cast<i32>(encoded));
        return false;
    }
    if(!AppendEncodedMip(compressor.get_uastc_backend_output(), mip, basisFormat, scratchArena, bytes)){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: Basis Universal returned an invalid mip layout."));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<PreparedTexture> EncodeLinearTexture(TextureEncodeInput&& input, Core::Alloc::ScratchArena& scratchArena){
    using namespace __hidden_encode_uastc;

    auto mipLevels = BuildMipLevels(input, scratchArena);
    if(!mipLevels)
        return MakeUnexpected(Failure{});
    const EncodedMip& lastMip = mipLevels->back();
    const u64 primaryByteCount = lastMip.offsetBytes + lastMip.sizeBytes;

    FloatMipChain mips;
    mips.reserve(mipLevels->size());
    mips.push_back(Move(input.planes));
    for(usize level = 1u; level < mipLevels->size(); ++level){
        auto next = GenerateNextMip(mips.back(), input.dimension);
        if(!next)
            return MakeUnexpected(Failure{});
        mips.push_back(Move(*next));
    }
    const auto alpha = DeriveAlpha(mips, input.format);
    if(!alpha || !InitializeEncoder())
        return MakeUnexpected(Failure{});

    const b8 separateAlpha = alpha->mode == TextureAlphaMode::SeparateUastcLdr4x4;
    const auto payloadByteCountResult = TryComputeTexConvPayloadByteCount(primaryByteCount, separateAlpha);
    if(!payloadByteCountResult)
        return MakeUnexpected(Failure{});
    const usize payloadByteCount = *payloadByteCountResult;

    PreparedTexture prepared;
    prepared.width = input.width;
    prepared.height = input.height;
    prepared.depth = input.depth;
    prepared.mipCount = static_cast<u32>(mipLevels->size());
    prepared.dimension = input.dimension;
    prepared.format = input.format;
    prepared.alphaMode = alpha->mode;
    prepared.alphaConstantUnorm8 = alpha->constantUnorm8;
    prepared.bytes.reserve(payloadByteCount);

    for(usize level = 0u; level < mips.size(); ++level){
        if(!EncodeMip(mips[level], input.dimension, input.format, false, (*mipLevels)[level], scratchArena, prepared.bytes))
            return MakeUnexpected(Failure{});
    }
    if(separateAlpha){
        for(usize level = 0u; level < mips.size(); ++level){
            if(!EncodeMip(mips[level], input.dimension, input.format, true, (*mipLevels)[level], scratchArena, prepared.bytes))
                return MakeUnexpected(Failure{});
        }
    }
    NWB_ASSERT(prepared.bytes.size() == payloadByteCount);
    return prepared;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TEX_CONV_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

