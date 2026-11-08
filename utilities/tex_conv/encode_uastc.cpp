// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "encode.h"
#include "texture_mip_preparer.h"

#include <core/common/log.h>

#include <global/simdmath.h>

#include <basisu_comp.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TEX_CONV_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_encode_uastc{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using EncodedMip = TextureFormat::MipLevelLayout;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static b8 InitializeEncoder(){
    // All mip encoders in this process share the initialized Basis tables.
    static const b8 s_Initialized = basisu::basisu_encoder_init(false);
    if(!s_Initialized)
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: failed to initialize Basis Universal."));
    return s_Initialized;
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

    auto mipLevels = TextureMipPreparer::buildLevels(input, scratchArena);
    if(!mipLevels)
        return MakeUnexpected(Failure{});
    const EncodedMip& lastMip = mipLevels->back();
    const u64 primaryByteCount = lastMip.offsetBytes + lastMip.sizeBytes;

    FloatMipChain mips;
    mips.reserve(mipLevels->size());
    mips.push_back(Move(input.planes));
    for(usize level = 1u; level < mipLevels->size(); ++level){
        auto next = TextureMipPreparer::generateNext(mips.back(), input.dimension);
        if(!next)
            return MakeUnexpected(Failure{});
        mips.push_back(Move(*next));
    }
    const auto alpha = TextureMipPreparer::deriveAlpha(mips, input.format);
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
