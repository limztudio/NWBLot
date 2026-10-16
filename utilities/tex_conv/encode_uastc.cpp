// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "encode.h"

#include <core/common/log.h>

#include <global/simdmath.h>

#include <basisu_comp.h>
#include <basisu_enc.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TEX_CONV_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_encode_uastc{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using FloatMipChain = basisu::vector<FloatPlanes>;
static constexpr u32 s_EncoderWorkerCount = 1u;
static constexpr f32 s_UnormScale = 255.0f;
static constexpr f32 s_UnormRoundingBias = 0.5f;
static constexpr AStringView s_MipFilter = "box";
static constexpr usize s_InvalidSlice = Limit<usize>::s_Max;

struct AlphaTransport{
    TextureAlphaMode::Enum mode = TextureAlphaMode::Opaque;
    u8 constantUnorm8 = TextureFormat::s_OpaqueAlphaUnorm8;
};

struct EncodedMip{
    u64 offsetBytes = 0u;
    u64 sizeBytes = 0u;
    u32 width = 0u;
    u32 height = 0u;
    u32 blockCountX = 0u;
    u32 blockCountY = 0u;
    u32 sliceCount = 1u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<Vector<EncodedMip>> BuildMipLevels(const TextureEncodeInput& entry){
    Vector<EncodedMip> mipLevels;

    const auto mipCountResult = TextureFormat::ComputeCompleteMipCount(entry.dimension, entry.width, entry.height, entry.depth);
    if(!mipCountResult){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: dimensions cannot form a complete mip chain."));
        return MakeUnexpected(Failure{});
    }
    mipLevels.reserve(*mipCountResult);
    u32 mipWidth = entry.width;
    u32 mipHeight = entry.height;
    u32 mipDepth = entry.depth;
    u64 offsetBytes = 0u;
    for(u32 mipIndex = 0u; mipIndex < *mipCountResult; ++mipIndex){
        const auto planeLayoutResult = TextureFormat::ComputeMipPlaneBlockLayout(entry.format, mipWidth, mipHeight);
        if(!planeLayoutResult){
            NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: mip {} block grid exceeds runtime limits."), mipIndex);
            return MakeUnexpected(Failure{});
        }
        const auto sliceCountResult = TextureFormat::ComputeMipSliceCount(entry.dimension, mipDepth);
        if(!sliceCountResult){
            NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: mip {} has an invalid slice count."), mipIndex);
            return MakeUnexpected(Failure{});
        }
        if(planeLayoutResult->planeByteCount > Limit<u64>::s_Max / *sliceCountResult){
            NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: mip {} byte size overflows."), mipIndex);
            return MakeUnexpected(Failure{});
        }
        const u64 sizeBytes = planeLayoutResult->planeByteCount * *sliceCountResult;
        if(sizeBytes > Limit<u64>::s_Max - offsetBytes){
            NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: mip payload offsets overflow."));
            return MakeUnexpected(Failure{});
        }

        EncodedMip mip;
        mip.width = mipWidth;
        mip.height = mipHeight;
        mip.blockCountX = planeLayoutResult->blocksX;
        mip.blockCountY = planeLayoutResult->blocksY;
        mip.offsetBytes = offsetBytes;
        mip.sizeBytes = sizeBytes;
        mip.sliceCount = *sliceCountResult;
        mipLevels.push_back(mip);

        offsetBytes += sizeBytes;
        mipWidth = mipWidth > 1u ? mipWidth >> 1u : 1u;
        mipHeight = mipHeight > 1u ? mipHeight >> 1u : 1u;
        if(entry.dimension == TextureDimension::Texture3D)
            mipDepth = mipDepth > 1u ? mipDepth >> 1u : 1u;
    }
    return mipLevels;
}

[[nodiscard]] static bool InitializeEncoder(){
    // All mip encoders in this process share the initialized Basis tables.
    static const bool s_Initialized = basisu::basisu_encoder_init(false);
    if(!s_Initialized)
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: failed to initialize Basis Universal."));
    return s_Initialized;
}

[[nodiscard]] static Expected<FloatPlanes> GenerateNextMip(const FloatPlanes& source, const TextureDimension::Enum dimension){
    if(source.empty())
        return MakeUnexpected(Failure{});
    const u32 sourceWidth = source.front().get_width();
    const u32 sourceHeight = source.front().get_height();
    const u32 targetWidth = sourceWidth > 1u ? sourceWidth >> 1u : 1u;
    const u32 targetHeight = sourceHeight > 1u ? sourceHeight >> 1u : 1u;
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
    const u32 targetDepth = sourceDepth >> 1u;
    FloatPlanes target;
    target.resize(targetDepth);
    for(u32 z = 0u; z < targetDepth; ++z){
        // Integer interval coordinates preserve fractional source-slab coverage when the depth is odd.
        const u64 intervalBegin = static_cast<u64>(z) * sourceDepth;
        const u64 intervalEnd = static_cast<u64>(z + 1u) * sourceDepth;
        const u32 first = static_cast<u32>(intervalBegin / targetDepth);
        const u32 end = static_cast<u32>(DivideUp(intervalEnd, static_cast<u64>(targetDepth)));
        basisu::imagef& plane = target[z];
        plane.resize(targetWidth, targetHeight);
        for(u32 y = 0u; y < targetHeight; ++y){
            for(u32 x = 0u; x < targetWidth; ++x){
                SIMDVectorSingle sum = VectorZero();
                for(u32 sourceZ = first; sourceZ < end; ++sourceZ){
                    const basisu::vec4F& color = filtered[sourceZ](x, y);
                    const u64 slabBegin = static_cast<u64>(sourceZ) * targetDepth;
                    const u64 slabEnd = static_cast<u64>(sourceZ + 1u) * targetDepth;
                    const f32 overlap = static_cast<f32>(Min(intervalEnd, slabEnd) - Max(intervalBegin, slabBegin));
                    sum = VectorAdd(sum, VectorScale(VectorSet(color[0u], color[1u], color[2u], color[3u]), overlap));
                }
                const SIMDVectorSingle mean = VectorScale(sum, 1.0f / static_cast<f32>(sourceDepth));
                plane(x, y).set(VectorGetX(mean), VectorGetY(mean), VectorGetZ(mean), VectorGetW(mean));
            }
        }
    }
    return target;
}

[[nodiscard]] static u8 QuantizeUnorm(const f32 value)noexcept{
    return static_cast<u8>(Saturate(value) * s_UnormScale + s_UnormRoundingBias);
}

[[nodiscard]] static Expected<AlphaTransport> DeriveAlpha(const FloatMipChain& mips, const TexturePayloadFormat::Enum format){
    AlphaTransport transport;
    bool found = false;
    bool constant = true;
    bool opaque = true;
    u8 firstAlpha = TextureFormat::s_OpaqueAlphaUnorm8;
    for(const FloatPlanes& planes : mips){
        for(const basisu::imagef& plane : planes){
            for(u32 y = 0u; y < plane.get_height(); ++y){
                for(u32 x = 0u; x < plane.get_width(); ++x){
                    const basisu::vec4F& color = plane(x, y);
                    if(!VectorIsFinite(VectorSet(color[0u], color[1u], color[2u], color[3u]), VectorComponentMask::s_XYZW)){
                        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: mip filtering produced non-finite values."));
                        return MakeUnexpected(Failure{});
                    }
                    const u8 alpha = QuantizeUnorm(color[3u]);
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

[[nodiscard]] static bool AppendEncodedMip(const basisu::basisu_backend_output& output, const EncodedMip& mip, const basist::basis_tex_format format, Vector<u8>& bytes, Core::Alloc::ScratchArena& scratchArena){
    if(output.m_tex_format != format || output.m_slice_desc.size() != mip.sliceCount || output.m_slice_desc.size() != output.m_slice_image_data.size())
        return false;

    ::Vector<usize, Core::Alloc::ScratchArena> slices(mip.sliceCount, s_InvalidSlice, scratchArena);
    const u64 planeByteCount = mip.sizeBytes / mip.sliceCount;
    for(usize index = 0u; index < output.m_slice_desc.size(); ++index){
        const basisu::basisu_backend_slice_desc& desc = output.m_slice_desc[index];
        if(desc.m_source_file_index >= mip.sliceCount || desc.m_mip_index != 0u || desc.m_orig_width != mip.width || desc.m_orig_height != mip.height
            || desc.m_num_blocks_x != mip.blockCountX || desc.m_num_blocks_y != mip.blockCountY || slices[desc.m_source_file_index] != s_InvalidSlice
            || output.m_slice_image_data[index].size_in_bytes() != planeByteCount)
            return false;
        slices[desc.m_source_file_index] = index;
    }
    if(mip.sizeBytes > Limit<usize>::s_Max || bytes.size() > Limit<usize>::s_Max - static_cast<usize>(mip.sizeBytes))
        return false;
    for(const usize index : slices){
        if(index == s_InvalidSlice)
            return false;
        const basisu::uint8_vec& blocks = output.m_slice_image_data[index];
        bytes.insert(bytes.end(), blocks.get_ptr(), blocks.get_ptr() + blocks.size_in_bytes());
    }
    return true;
}

[[nodiscard]] static bool EncodeMip(const FloatPlanes& planes, const TextureDimension::Enum dimension, const TexturePayloadFormat::Enum format, const bool alphaOnly, const EncodedMip& mip, Vector<u8>& bytes, Core::Alloc::ScratchArena& scratchArena){
    basisu::job_pool jobs(s_EncoderWorkerCount);
    basisu::basis_compressor_params params;
    const bool hdr = !alphaOnly && IsHdrTexturePayloadFormat(format);
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
                    if(alphaOnly){
                        const u8 alpha = QuantizeUnorm(color[3u]);
                        target(x, y) = basisu::color_rgba(alpha, alpha, alpha, TextureFormat::s_OpaqueAlphaUnorm8);
                    }
                    else
                        target(x, y) = basisu::color_rgba(QuantizeUnorm(color[0u]), QuantizeUnorm(color[1u]), QuantizeUnorm(color[2u]), QuantizeUnorm(color[3u]));
                }
            }
        }
    }

    basisu::basis_compressor compressor;
    if(!compressor.init(params) || compressor.process() != basisu::basis_compressor::cECSuccess){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: Basis Universal failed to encode a linear mip."));
        return false;
    }
    if(!AppendEncodedMip(compressor.get_uastc_backend_output(), mip, basisFormat, bytes, scratchArena)){
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

    auto mipLevels = BuildMipLevels(input);
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

    const bool separateAlpha = alpha->mode == TextureAlphaMode::SeparateUastcLdr4x4;
    if(primaryByteCount > Limit<usize>::s_Max / (separateAlpha ? 2u : 1u))
        return MakeUnexpected(Failure{});
    const usize payloadByteCount = static_cast<usize>(primaryByteCount) * (separateAlpha ? 2u : 1u);

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
        if(!EncodeMip(mips[level], input.dimension, input.format, false, (*mipLevels)[level], prepared.bytes, scratchArena))
            return MakeUnexpected(Failure{});
    }
    if(separateAlpha){
        for(usize level = 0u; level < mips.size(); ++level){
            if(!EncodeMip(mips[level], input.dimension, input.format, true, (*mipLevels)[level], prepared.bytes, scratchArena))
                return MakeUnexpected(Failure{});
        }
    }
    if(prepared.bytes.size() != payloadByteCount){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: encoded texture does not match its binary payload layout."));
        return MakeUnexpected(Failure{});
    }
    return prepared;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TEX_CONV_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

