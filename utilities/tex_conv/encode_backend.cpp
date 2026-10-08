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


void ConfigureCompressor(
    basisu::basis_compressor_params& parameters,
    basisu::job_pool& jobPool,
    const basist::basis_tex_format format,
    const bool srgb
){
    parameters.set_format_mode(format);
    parameters.set_srgb_options(srgb);
    parameters.m_status_output = false;
    parameters.m_compute_stats = false;
    parameters.m_print_stats = false;
    parameters.m_write_output_basis_or_ktx2_files = false;
    parameters.m_create_ktx2_file = false;
    parameters.m_pJob_pool = &jobPool;
}

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
    outPayload.alphaConstantUnorm8 = TextureFormat::s_OpaqueAlphaUnorm8;
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

// Basis slices are source-major; m_source_file_index restores on-disk plane order.


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

    const auto layout = TextureFormat::ComputeMipPlaneBlockLayout(inOutPayload.format, width, height);
    if(!layout || layout->planeByteCount > Limit<usize>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: UASTC mip block layout exceeds supported limits."));
        return false;
    }
    const u32 blocksX = layout->blocksX;
    const u32 blocksY = layout->blocksY;
    const u64 planeByteCount = layout->planeByteCount;
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



[[nodiscard]] Expected<basisu::imagef> LoadAlphaMask(
    const AlphaSource& alphaSource,
    const u32 expectedWidth,
    const u32 expectedHeight
){
    basisu::imagef mask;
    if(alphaSource.mode != AlphaSourceMode::Image)
        return mask;

    const AString alphaPathText = PathToGenericString<AString>(alphaSource.path);
    if(IsHdrInputPath(alphaSource.path)){
        if(!basisu::load_image_hdr(alphaPathText.c_str(), mask, false)){
            NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: failed to decode alpha image '{}'."), PathToString<tchar>(alphaSource.path));
            return MakeUnexpected(Failure{});
        }
    }
    else{
        basisu::image sourceMask;
        if(!basisu::load_image(alphaPathText.c_str(), sourceMask)){
            NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: failed to decode alpha image '{}'."), PathToString<tchar>(alphaSource.path));
            return MakeUnexpected(Failure{});
        }
        mask.resize(sourceMask.get_width(), sourceMask.get_height());
        for(u32 y = 0u; y < sourceMask.get_height(); ++y){
            for(u32 x = 0u; x < sourceMask.get_width(); ++x)
                mask(x, y)[0u] = static_cast<f32>(sourceMask(x, y).r) / s_BasisColorChannelMax;
        }
    }
    if(mask.get_width() != expectedWidth || mask.get_height() != expectedHeight){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: alpha image resolution must match the texture input resolution."));
        return MakeUnexpected(Failure{});
    }

    for(u32 y = 0u; y < expectedHeight; ++y){
        u32 x = 0u;
        const u32 chunkEndX = expectedWidth & ~3u;
        for(; x < chunkEndX; x += 4u){
            const f32 alpha0 = mask(x, y)[0u];
            const f32 alpha1 = mask(x + 1u, y)[0u];
            const f32 alpha2 = mask(x + 2u, y)[0u];
            const f32 alpha3 = mask(x + 3u, y)[0u];
            if(!IsFinite(alpha0) || !IsFinite(alpha1) || !IsFinite(alpha2) || !IsFinite(alpha3)){
                NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: alpha image contains a non-finite red-channel value."));
                return MakeUnexpected(Failure{});
            }
            const SIMDVector saturatedLanes = VectorSaturate(VectorSet(alpha0, alpha1, alpha2, alpha3));
            mask(x, y)[0u] = VectorGetX(saturatedLanes);
            mask(x + 1u, y)[0u] = VectorGetY(saturatedLanes);
            mask(x + 2u, y)[0u] = VectorGetZ(saturatedLanes);
            mask(x + 3u, y)[0u] = VectorGetW(saturatedLanes);
        }
        for(; x < expectedWidth; ++x){
            f32& alpha = mask(x, y)[0u];
            if(!IsFinite(alpha)){
                NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: alpha image contains a non-finite red-channel value."));
                return MakeUnexpected(Failure{});
            }
            alpha = VectorGetX(VectorSaturate(VectorReplicate(alpha)));
        }
    }
    return mask;
}

[[nodiscard]] Expected<VolumeMipDims> ComputeVolumeMipDims(u32 sourceWidth, u32 sourceHeight, u32 sourceDepth)noexcept{
    VolumeMipDims dims;
    if(sourceWidth == 0u || sourceHeight == 0u || sourceDepth == 0u)
        return MakeUnexpected(Failure{});
    dims.sourceWidth = sourceWidth;
    dims.sourceHeight = sourceHeight;
    dims.sourceDepth = sourceDepth;
    dims.targetWidth = sourceWidth > 1u ? sourceWidth >> 1u : 1u;
    dims.targetHeight = sourceHeight > 1u ? sourceHeight >> 1u : 1u;
    dims.targetDepth = sourceDepth > 1u ? sourceDepth >> 1u : 1u;
    return dims;
}

[[nodiscard]] Expected<VolumeMipSliceRange> ComputeVolumeMipSliceRange(u32 sourceDepth, u32 targetDepth, u32 targetZ)noexcept{
    if(sourceDepth == 0u || targetDepth == 0u || targetZ >= targetDepth)
        return MakeUnexpected(Failure{});
    const u32 sourceFirst = static_cast<u32>((static_cast<u64>(targetZ) * sourceDepth) / targetDepth);
    u32 sourceEnd = static_cast<u32>((static_cast<u64>(targetZ + 1u) * sourceDepth) / targetDepth);
    if(sourceEnd <= sourceFirst)
        sourceEnd = sourceFirst + 1u;
    const u32 end = Min(sourceEnd, sourceDepth);
    if(sourceFirst >= end)
        return MakeUnexpected(Failure{});
    return VolumeMipSliceRange{ sourceFirst, end };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TEX_CONV_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

