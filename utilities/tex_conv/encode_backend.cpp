// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "encode_backend.h"

#include <core/common/log.h>

#include <global/simdmath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TEX_CONV_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace EncodeBackendDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ResetPayload(
    TexturePayload& outPayload,
    const TextureDimension::Enum dimension,
    const u32 width,
    const u32 height,
    const u32 depth,
    const TexturePayloadFormat::Enum format,
    const bool srgb
){
    outPayload.dimension = dimension;
    outPayload.width = width;
    outPayload.height = height;
    outPayload.depth = depth;
    outPayload.format = format;
    outPayload.srgb = srgb;
    outPayload.hasAlpha = false;
    outPayload.alphaMode = TextureAlphaMode::Opaque;
    outPayload.alphaConstantUnorm8 = s_OpaqueAlphaUnorm8;
    outPayload.mips.clear();
    outPayload.bytes.clear();
    outPayload.alphaBytes.clear();
}



[[nodiscard]] bool ValidateBackendOutput(
    const basisu::basisu_backend_output& backendOutput,
    const basist::basis_tex_format expectedFormat
){
    if(backendOutput.m_tex_format != expectedFormat){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: Basis Universal produced an unexpected UASTC block format."));
        return false;
    }
    if(backendOutput.m_slice_desc.empty() || backendOutput.m_slice_desc.size() != backendOutput.m_slice_image_data.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: Basis Universal returned an incomplete UASTC slice payload."));
        return false;
    }
    return true;
}

// Appends one raw mip in the on-disk canonical order: plane 0 through planeCount-1. Basis emits source-major slices,
// so this deliberately uses m_source_file_index rather than the physical backend ordering.


[[nodiscard]] bool AppendCanonicalMip(
    const basisu::basisu_backend_output& backendOutput,
    const u32 backendMipIndex,
    const u32 planeCount,
    const u32 width,
    const u32 height,
    TexturePayload& inOutPayload
){
    if(planeCount == 0u || backendOutput.m_slice_desc.size() != backendOutput.m_slice_image_data.size())
        return false;

    u32 blocksX = 0u;
    u32 blocksY = 0u;
    u64 planeByteCount = 0u;
    if(!ComputeMipPlaneBlockLayout(
        inOutPayload.format,
        width,
        height,
        blocksX,
        blocksY,
        planeByteCount
    ) || planeByteCount > Limit<usize>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: UASTC mip block layout exceeds supported limits."));
        return false;
    }
    if(planeByteCount > Limit<u64>::s_Max / planeCount){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: UASTC mip payload size overflowed."));
        return false;
    }
    const u64 mipByteCount = planeByteCount * planeCount;
    if(mipByteCount > Limit<usize>::s_Max || inOutPayload.bytes.size() > Limit<usize>::s_Max - static_cast<usize>(mipByteCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: UASTC payload is too large to store."));
        return false;
    }

    Vector<usize> sliceForPlane(planeCount, s_InvalidBackendSlice);
    for(usize backendSliceIndex = 0u; backendSliceIndex < backendOutput.m_slice_desc.size(); ++backendSliceIndex){
        const basisu::basisu_backend_slice_desc& descriptor = backendOutput.m_slice_desc[backendSliceIndex];
        if(descriptor.m_mip_index != backendMipIndex)
            continue;
        if(
            descriptor.m_source_file_index >= planeCount
            || descriptor.m_orig_width != width
            || descriptor.m_orig_height != height
            || descriptor.m_num_blocks_x != blocksX
            || descriptor.m_num_blocks_y != blocksY
            || sliceForPlane[descriptor.m_source_file_index] != s_InvalidBackendSlice
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: Basis Universal returned an unexpected texture-slice layout."));
            return false;
        }

        const basisu::uint8_vec& encodedBlocks = backendOutput.m_slice_image_data[backendSliceIndex];
        if(encodedBlocks.size_in_bytes() != planeByteCount){
            NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: Basis Universal produced an invalid UASTC block layout."));
            return false;
        }
        sliceForPlane[descriptor.m_source_file_index] = backendSliceIndex;
    }

    for(u32 planeIndex = 0u; planeIndex < planeCount; ++planeIndex){
        if(sliceForPlane[planeIndex] == s_InvalidBackendSlice){
            NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: Basis Universal omitted a required UASTC plane."));
            return false;
        }
    }

    MipLevel mip;
    mip.level = static_cast<u32>(inOutPayload.mips.size());
    mip.width = width;
    mip.height = height;
    mip.blocksX = blocksX;
    mip.blocksY = blocksY;
    mip.offsetBytes = static_cast<u64>(inOutPayload.bytes.size());
    mip.sizeBytes = mipByteCount;
    mip.sliceCount = planeCount;
    inOutPayload.mips.push_back(mip);

    for(u32 planeIndex = 0u; planeIndex < planeCount; ++planeIndex){
        const basisu::uint8_vec& encodedBlocks = backendOutput.m_slice_image_data[sliceForPlane[planeIndex]];
        const u8* const source = encodedBlocks.get_ptr();
        inOutPayload.bytes.insert(inOutPayload.bytes.end(), source, source + encodedBlocks.size_in_bytes());
    }
    return true;
}



[[nodiscard]] bool LoadAlphaMask(
    const AlphaSource& alphaSource,
    const u32 expectedWidth,
    const u32 expectedHeight,
    basisu::imagef& outMask
){
    if(alphaSource.mode != AlphaSourceMode::Image)
        return true;

    const AString alphaPathText = PathToGenericString<AString>(alphaSource.path);
    if(IsHdrInputPath(alphaSource.path)){
        if(!basisu::load_image_hdr(alphaPathText.c_str(), outMask, false)){
            NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: failed to decode alpha image '{}'."), PathToString<tchar>(alphaSource.path));
            return false;
        }
    }
    else{
        basisu::image sourceMask;
        if(!basisu::load_image(alphaPathText.c_str(), sourceMask)){
            NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: failed to decode alpha image '{}'."), PathToString<tchar>(alphaSource.path));
            return false;
        }
        outMask.resize(sourceMask.get_width(), sourceMask.get_height());
        for(u32 y = 0u; y < sourceMask.get_height(); ++y){
            for(u32 x = 0u; x < sourceMask.get_width(); ++x)
                outMask(x, y)[0u] = static_cast<f32>(sourceMask(x, y).r) / s_BasisColorChannelMax;
        }
    }
    if(outMask.get_width() != expectedWidth || outMask.get_height() != expectedHeight){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: alpha image resolution must match the texture input resolution."));
        return false;
    }

    for(u32 y = 0u; y < expectedHeight; ++y){
        for(u32 x = 0u; x < expectedWidth; ++x){
            f32& alpha = outMask(x, y)[0u];
            if(!IsFinite(alpha)){
                NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: alpha image contains a non-finite red-channel value."));
                return false;
            }
            alpha = Saturate(alpha);
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TEX_CONV_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

