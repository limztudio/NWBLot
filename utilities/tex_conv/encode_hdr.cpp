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


[[nodiscard]] bool GetBasisTextureType(
    const TextureDimension::Enum dimension,
    basist::basis_texture_type& outTextureType
){
    switch(dimension){
    case TextureDimension::Texture2D:
        outTextureType = basist::cBASISTexType2D;
        return true;
    case TextureDimension::TextureCube:
        outTextureType = basist::cBASISTexTypeCubemapArray;
        return true;
    case TextureDimension::Texture3D:
        outTextureType = basist::cBASISTexTypeVolume;
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool ValidHdrRgb(const SIMDVector rgb){
    return
        Vector3IsFinite(rgb)
        && Vector3GreaterOrEqual(rgb, VectorZero())
        && Vector3LessOrEqual(rgb, VectorReplicate(s_UastcHdrMaximum))
    ;
}

[[nodiscard]] bool ValidateHdrRgbPlanes(const HdrImagePlanes& planes){
    if(planes.empty() || planes.size() > Limit<u32>::s_Max)
        return false;

    const u32 width = planes.front().get_width();
    const u32 height = planes.front().get_height();
    if(width == 0u || height == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: HDR image has an invalid resolution."));
        return false;
    }
    for(const basisu::imagef& plane : planes){
        if(plane.get_width() != width || plane.get_height() != height){
            NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: HDR image planes have inconsistent dimensions."));
            return false;
        }
        for(u32 y = 0u; y < height; ++y){
            for(u32 x = 0u; x < width; ++x){
                const basisu::vec4F& color = plane(x, y);
                if(!ValidHdrRgb(VectorSet(color[0u], color[1u], color[2u], 0.0f))){
                    NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: HDR RGB input must contain finite values in [0, 65216]."));
                    return false;
                }
            }
        }
    }
    return true;
}

[[nodiscard]] bool LoadHdrPlanes(const Vector<Path>& inputPaths, HdrImagePlanes& outPlanes){
    outPlanes.clear();
    if(inputPaths.empty())
        return false;

    u32 width = 0u;
    u32 height = 0u;
    outPlanes.reserve(inputPaths.size());
    for(const Path& inputPath : inputPaths){
        const AString inputPathText = PathToGenericString<AString>(inputPath);
        basisu::imagef plane;
        if(!basisu::load_image_hdr(inputPathText.c_str(), plane, false)){
            NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: failed to decode HDR image '{}'."), PathToString<tchar>(inputPath));
            return false;
        }
        if(plane.get_width() == 0u || plane.get_height() == 0u){
            NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: HDR image '{}' has an invalid resolution."), PathToString<tchar>(inputPath));
            return false;
        }
        if(outPlanes.empty()){
            width = plane.get_width();
            height = plane.get_height();
        }
        else if(plane.get_width() != width || plane.get_height() != height){
            NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: all HDR texture inputs must have the same resolution."));
            return false;
        }
        outPlanes.push_back(Move(plane));
    }
    return true;
}

[[nodiscard]] bool ApplyHdrAlphaSource(const AlphaSource& alphaSource, HdrImagePlanes& inOutPlanes){
    if(inOutPlanes.empty())
        return false;

    const u32 width = inOutPlanes.front().get_width();
    const u32 height = inOutPlanes.front().get_height();
    for(const basisu::imagef& plane : inOutPlanes){
        if(plane.get_width() != width || plane.get_height() != height){
            NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: HDR texture inputs have inconsistent resolutions."));
            return false;
        }
    }

    basisu::imagef alphaMask;
    if(alphaSource.mode == AlphaSourceMode::Image && !LoadAlphaMask(alphaSource, width, height, alphaMask))
        return false;
    if(alphaSource.mode == AlphaSourceMode::Constant && !IsFinite(alphaSource.constant)){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: alpha constant must be finite."));
        return false;
    }

    for(basisu::imagef& plane : inOutPlanes){
        for(u32 y = 0u; y < height; ++y){
            for(u32 x = 0u; x < width; ++x){
                f32 alpha = 1.0f;
                switch(alphaSource.mode){
                case AlphaSourceMode::Original:
                    alpha = plane(x, y)[3u];
                    if(!IsFinite(alpha)){
                        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: HDR input contains a non-finite alpha value."));
                        return false;
                    }
                    break;
                case AlphaSourceMode::Constant:
                    alpha = alphaSource.constant;
                    break;
                case AlphaSourceMode::Image:
                    alpha = alphaMask(x, y)[0u];
                    break;
                default:
                    NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: unsupported alpha source."));
                    return false;
                }
                plane(x, y)[3u] = Saturate(alpha);
            }
        }
    }
    return true;
}

[[nodiscard]] bool GenerateNextHdrMip(const HdrImagePlanes& sourcePlanes, HdrImagePlanes& outPlanes){
    if(sourcePlanes.empty())
        return false;

    const u32 sourceWidth = sourcePlanes.front().get_width();
    const u32 sourceHeight = sourcePlanes.front().get_height();
    const u32 targetWidth = sourceWidth > 1u ? sourceWidth >> 1u : 1u;
    const u32 targetHeight = sourceHeight > 1u ? sourceHeight >> 1u : 1u;
    outPlanes.clear();
    outPlanes.resize(sourcePlanes.size());
    for(usize planeIndex = 0u; planeIndex < sourcePlanes.size(); ++planeIndex){
        const basisu::imagef& source = sourcePlanes[planeIndex];
        basisu::imagef& target = outPlanes[planeIndex];
        target.resize(targetWidth, targetHeight);
        if(!basisu::image_resample(source, target, "box", 1.0f, false, 0u, s_HdrChannelCount)){
            NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: failed to generate an HDR mip level."));
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool GenerateNextHdrVolumeMip(const HdrImagePlanes& sourcePlanes, HdrImagePlanes& outPlanes){
    if(sourcePlanes.empty())
        return false;

    const u32 sourceWidth = sourcePlanes.front().get_width();
    const u32 sourceHeight = sourcePlanes.front().get_height();
    const u32 sourceDepth = static_cast<u32>(sourcePlanes.size());
    const u32 targetWidth = sourceWidth > 1u ? sourceWidth >> 1u : 1u;
    const u32 targetHeight = sourceHeight > 1u ? sourceHeight >> 1u : 1u;
    const u32 targetDepth = sourceDepth > 1u ? sourceDepth >> 1u : 1u;
    outPlanes.clear();
    outPlanes.resize(targetDepth);

    for(u32 targetZ = 0u; targetZ < targetDepth; ++targetZ){
        const u32 sourceFirst = static_cast<u32>((static_cast<u64>(targetZ) * sourceDepth) / targetDepth);
        u32 sourceEnd = static_cast<u32>((static_cast<u64>(targetZ + 1u) * sourceDepth) / targetDepth);
        if(sourceEnd <= sourceFirst)
            sourceEnd = sourceFirst + 1u;
        sourceEnd = Min(sourceEnd, sourceDepth);

        HdrImagePlanes filteredPlanes;
        filteredPlanes.resize(sourceEnd - sourceFirst);
        for(u32 sourceZ = sourceFirst; sourceZ < sourceEnd; ++sourceZ){
            basisu::imagef& filteredPlane = filteredPlanes[sourceZ - sourceFirst];
            filteredPlane.resize(targetWidth, targetHeight);
            if(!basisu::image_resample(
                sourcePlanes[sourceZ],
                filteredPlane,
                "box",
                1.0f,
                false,
                0u,
                s_HdrChannelCount
            )){
                NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: failed to generate an HDR volume mip level."));
                return false;
            }
        }

        const u32 filteredPlaneCount = static_cast<u32>(filteredPlanes.size());
        basisu::imagef& targetPlane = outPlanes[targetZ];
        targetPlane.resize(targetWidth, targetHeight);
        for(u32 y = 0u; y < targetHeight; ++y){
            for(u32 x = 0u; x < targetWidth; ++x){
                SIMDVector sum = VectorZero();
                for(const basisu::imagef& filteredPlane : filteredPlanes){
                    const basisu::vec4F& sourceColor = filteredPlane(x, y);
                    const Float4 sourceTexel(sourceColor[0u], sourceColor[1u], sourceColor[2u], sourceColor[3u]);
                    sum = VectorAdd(sum, LoadFloat(sourceTexel));
                }

                Float4 averageTexel = {};
                StoreFloat(VectorScale(sum, 1.0f / static_cast<f32>(filteredPlaneCount)), averageTexel);
                basisu::vec4F& targetColor = targetPlane(x, y);
                targetColor[0u] = averageTexel.r;
                targetColor[1u] = averageTexel.g;
                targetColor[2u] = averageTexel.b;
                targetColor[3u] = averageTexel.a;
            }
        }
    }
    return true;
}

[[nodiscard]] bool ExtractHdrAlphaMips(
    HdrVolumeMips& inOutMipPlanes,
    VolumeMips& outAlphaMips,
    TextureAlphaMode::Enum& outAlphaMode,
    u8& outAlphaConstantUnorm8
){
    if(inOutMipPlanes.empty())
        return false;

    bool foundAlpha = false;
    bool allOpaque = true;
    bool allConstant = true;
    u8 constantAlpha = s_OpaqueAlphaUnorm8;
    outAlphaMips.clear();
    outAlphaMips.resize(inOutMipPlanes.size());
    for(usize mipIndex = 0u; mipIndex < inOutMipPlanes.size(); ++mipIndex){
        HdrImagePlanes& hdrPlanes = inOutMipPlanes[mipIndex];
        if(!ValidateHdrRgbPlanes(hdrPlanes))
            return false;

        ImagePlanes& alphaPlanes = outAlphaMips[mipIndex];
        alphaPlanes.resize(hdrPlanes.size());
        const u32 width = hdrPlanes.front().get_width();
        const u32 height = hdrPlanes.front().get_height();
        for(usize planeIndex = 0u; planeIndex < hdrPlanes.size(); ++planeIndex){
            basisu::imagef& hdrPlane = hdrPlanes[planeIndex];
            basisu::image& alphaPlane = alphaPlanes[planeIndex];
            alphaPlane.resize(width, height);
            for(u32 y = 0u; y < height; ++y){
                for(u32 x = 0u; x < width; ++x){
                    basisu::vec4F& hdrColor = hdrPlane(x, y);
                    const f32 alpha = hdrColor[3u];
                    if(!IsFinite(alpha)){
                        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: HDR mip generation produced a non-finite alpha value."));
                        return false;
                    }
                    const u8 quantizedAlpha = static_cast<u8>(
                        Saturate(alpha) * s_BasisColorChannelMax + s_BasisColorChannelRoundingBias
                    );
                    alphaPlane(x, y) = basisu::color_rgba(
                        quantizedAlpha,
                        quantizedAlpha,
                        quantizedAlpha,
                        s_OpaqueAlphaUnorm8
                    );
                    hdrColor[3u] = 1.0f;

                    if(!foundAlpha){
                        foundAlpha = true;
                        constantAlpha = quantizedAlpha;
                    }
                    else if(quantizedAlpha != constantAlpha)
                        allConstant = false;
                    if(quantizedAlpha != s_OpaqueAlphaUnorm8)
                        allOpaque = false;
                }
            }
        }
    }
    if(!foundAlpha)
        return false;

    if(allOpaque){
        outAlphaMode = TextureAlphaMode::Opaque;
        outAlphaConstantUnorm8 = s_OpaqueAlphaUnorm8;
    }
    else if(allConstant){
        outAlphaMode = TextureAlphaMode::ConstantUnorm8;
        outAlphaConstantUnorm8 = constantAlpha;
    }
    else{
        outAlphaMode = TextureAlphaMode::SeparateUastcLdr4x4;
        outAlphaConstantUnorm8 = s_OpaqueAlphaUnorm8;
    }
    return true;
}

[[nodiscard]] bool EncodeHdrMip(
    const HdrImagePlanes& planes,
    const TextureDimension::Enum dimension,
    TexturePayload& inOutPayload
){
    if(!ValidateHdrRgbPlanes(planes))
        return false;

    basist::basis_texture_type textureType = basist::cBASISTexType2D;
    if(!GetBasisTextureType(dimension, textureType))
        return false;

    const u32 width = planes.front().get_width();
    const u32 height = planes.front().get_height();
    basisu::job_pool jobPool(s_BasisEncoderWorkerCount);
    basisu::basis_compressor_params parameters;
    parameters.set_format_mode(basist::basis_tex_format::cUASTC_HDR_4x4);
    parameters.set_srgb_options(false);
    parameters.m_read_source_images = false;
    parameters.m_tex_type = textureType;
    parameters.m_source_images_hdr = planes;
    parameters.m_mip_gen = false;
    parameters.m_status_output = false;
    parameters.m_compute_stats = false;
    parameters.m_print_stats = false;
    parameters.m_write_output_basis_or_ktx2_files = false;
    parameters.m_create_ktx2_file = false;
    parameters.m_pJob_pool = &jobPool;

    basisu::basis_compressor compressor;
    if(!compressor.init(parameters)){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: Basis Universal failed to initialize an HDR mip encoder."));
        return false;
    }
    const basisu::basis_compressor::error_code encodeResult = compressor.process();
    if(encodeResult != basisu::basis_compressor::cECSuccess){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: UASTC HDR mip encoding failed (Basis Universal error {}).")
            , static_cast<u32>(encodeResult)
        );
        return false;
    }

    const basisu::basisu_backend_output& backendOutput = compressor.get_uastc_backend_output();
    if(
        !ValidateBackendOutput(backendOutput, basist::basis_tex_format::cUASTC_HDR_4x4)
        || backendOutput.m_slice_desc.size() != planes.size()
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: Basis Universal returned an incomplete UASTC HDR mip."));
        return false;
    }
    return AppendCanonicalMip(backendOutput, 0u, static_cast<u32>(planes.size()), width, height, inOutPayload);
}

[[nodiscard]] bool EncodeHdrAlphaMip(
    const ImagePlanes& planes,
    const TextureDimension::Enum dimension,
    TexturePayload& inOutPayload
){
    if(planes.empty() || planes.size() > Limit<u32>::s_Max)
        return false;

    const u32 width = planes.front().get_width();
    const u32 height = planes.front().get_height();
    if(width == 0u || height == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: HDR alpha mip has an invalid resolution."));
        return false;
    }
    for(const basisu::image& plane : planes){
        if(plane.get_width() != width || plane.get_height() != height){
            NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: HDR alpha mip planes have inconsistent dimensions."));
            return false;
        }
    }

    basist::basis_texture_type textureType = basist::cBASISTexType2D;
    if(!GetBasisTextureType(dimension, textureType))
        return false;

    basisu::job_pool jobPool(s_BasisEncoderWorkerCount);
    basisu::basis_compressor_params parameters;
    parameters.set_format_mode(basist::basis_tex_format::cUASTC_LDR_4x4);
    parameters.set_srgb_options(false);
    parameters.m_read_source_images = false;
    parameters.m_tex_type = textureType;
    parameters.m_source_images = planes;
    parameters.m_mip_gen = false;
    parameters.m_status_output = false;
    parameters.m_compute_stats = false;
    parameters.m_print_stats = false;
    parameters.m_write_output_basis_or_ktx2_files = false;
    parameters.m_create_ktx2_file = false;
    parameters.m_pJob_pool = &jobPool;

    basisu::basis_compressor compressor;
    if(!compressor.init(parameters)){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: Basis Universal failed to initialize an HDR alpha mip encoder."));
        return false;
    }
    const basisu::basis_compressor::error_code encodeResult = compressor.process();
    if(encodeResult != basisu::basis_compressor::cECSuccess){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: UASTC HDR alpha mip encoding failed (Basis Universal error {}).")
            , static_cast<u32>(encodeResult)
        );
        return false;
    }

    const basisu::basisu_backend_output& backendOutput = compressor.get_uastc_backend_output();
    if(
        !ValidateBackendOutput(backendOutput, basist::basis_tex_format::cUASTC_LDR_4x4)
        || backendOutput.m_slice_desc.size() != planes.size()
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: Basis Universal returned an incomplete UASTC HDR alpha mip."));
        return false;
    }
    return AppendCanonicalMip(backendOutput, 0u, static_cast<u32>(planes.size()), width, height, inOutPayload);
}

[[nodiscard]] bool ValidateHdrAlphaPayloadLayout(
    const TexturePayload& primaryPayload,
    const TexturePayload& alphaPayload
){
    if(
        primaryPayload.bytes.size() != alphaPayload.bytes.size()
        || primaryPayload.mips.size() != alphaPayload.mips.size()
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: UASTC HDR alpha payload does not match the RGB payload size."));
        return false;
    }
    for(usize mipIndex = 0u; mipIndex < primaryPayload.mips.size(); ++mipIndex){
        const MipLevel& primaryMip = primaryPayload.mips[mipIndex];
        const MipLevel& alphaMip = alphaPayload.mips[mipIndex];
        if(
            primaryMip.level != alphaMip.level
            || primaryMip.width != alphaMip.width
            || primaryMip.height != alphaMip.height
            || primaryMip.blocksX != alphaMip.blocksX
            || primaryMip.blocksY != alphaMip.blocksY
            || primaryMip.offsetBytes != alphaMip.offsetBytes
            || primaryMip.sizeBytes != alphaMip.sizeBytes
            || primaryMip.sliceCount != alphaMip.sliceCount
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: UASTC HDR alpha payload mip layout does not match RGB."));
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool EncodeHdrAlphaMips(
    const VolumeMips& alphaMips,
    const TextureDimension::Enum dimension,
    const u32 width,
    const u32 height,
    const u32 depth,
    TexturePayload& inOutPayload
){
    if(alphaMips.empty())
        return false;

    TexturePayload alphaPayload;
    ResetPayload(
        alphaPayload,
        dimension,
        width,
        height,
        depth,
        TexturePayloadFormat::UastcLdr4x4,
        false
    );
    alphaPayload.mips.reserve(alphaMips.size());
    for(const ImagePlanes& alphaMip : alphaMips){
        if(!EncodeHdrAlphaMip(alphaMip, dimension, alphaPayload))
            return false;
    }
    if(!ValidateHdrAlphaPayloadLayout(inOutPayload, alphaPayload))
        return false;

    inOutPayload.alphaBytes = Move(alphaPayload.bytes);
    return true;
}

[[nodiscard]] bool EncodeHdrMipChain(
    HdrVolumeMips& inOutMipPlanes,
    const TextureDimension::Enum dimension,
    const u32 width,
    const u32 height,
    const u32 depth,
    TexturePayload& outPayload
){
    if(inOutMipPlanes.empty())
        return false;

    VolumeMips alphaMips;
    TextureAlphaMode::Enum alphaMode = TextureAlphaMode::Opaque;
    u8 alphaConstantUnorm8 = s_OpaqueAlphaUnorm8;
    if(!ExtractHdrAlphaMips(inOutMipPlanes, alphaMips, alphaMode, alphaConstantUnorm8))
        return false;

    ResetPayload(
        outPayload,
        dimension,
        width,
        height,
        depth,
        TexturePayloadFormat::UastcHdr4x4,
        false
    );
    outPayload.mips.reserve(inOutMipPlanes.size());
    for(const HdrImagePlanes& mipPlanes : inOutMipPlanes){
        if(!EncodeHdrMip(mipPlanes, dimension, outPayload))
            return false;
    }

    outPayload.alphaMode = alphaMode;
    outPayload.alphaConstantUnorm8 = alphaConstantUnorm8;
    outPayload.hasAlpha = alphaMode != TextureAlphaMode::Opaque;
    if(alphaMode == TextureAlphaMode::SeparateUastcLdr4x4){
        if(!EncodeHdrAlphaMips(alphaMips, dimension, width, height, depth, outPayload))
            return false;
    }
    return true;
}

[[nodiscard]] bool EncodeHdr2DOrCube(
    const Vector<Path>& inputPaths,
    const TextureDimension::Enum dimension,
    const AlphaSource& alphaSource,
    TexturePayload& outPayload
){
    const u32 planeCount = dimension == TextureDimension::TextureCube ? s_TextureCubeFaceCount : 1u;
    if(inputPaths.size() != planeCount)
        return false;

    HdrImagePlanes sourcePlanes;
    if(
        !LoadHdrPlanes(inputPaths, sourcePlanes)
        || !ValidateHdrRgbPlanes(sourcePlanes)
        || !ApplyHdrAlphaSource(alphaSource, sourcePlanes)
    )
        return false;
    const u32 width = sourcePlanes.front().get_width();
    const u32 height = sourcePlanes.front().get_height();
    if(dimension == TextureDimension::TextureCube && width != height){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: HDR cubemap faces must be square."));
        return false;
    }

    u32 mipCount = 0u;
    if(!ComputeCompleteMipCount(dimension, width, height, 1u, mipCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: HDR texture dimensions cannot form a complete mip chain."));
        return false;
    }

    HdrVolumeMips mipPlanes;
    mipPlanes.resize(mipCount);
    mipPlanes[0u] = Move(sourcePlanes);
    for(u32 mipIndex = 1u; mipIndex < mipCount; ++mipIndex){
        if(!GenerateNextHdrMip(mipPlanes[mipIndex - 1u], mipPlanes[mipIndex]))
            return false;
    }
    return EncodeHdrMipChain(mipPlanes, dimension, width, height, 1u, outPayload);
}

[[nodiscard]] bool EncodeHdrVolume(
    const Vector<Path>& inputPaths,
    const AlphaSource& alphaSource,
    TexturePayload& outPayload
){
    HdrImagePlanes sourcePlanes;
    if(
        !LoadHdrPlanes(inputPaths, sourcePlanes)
        || !ValidateHdrRgbPlanes(sourcePlanes)
        || !ApplyHdrAlphaSource(alphaSource, sourcePlanes)
    )
        return false;

    const u32 width = sourcePlanes.front().get_width();
    const u32 height = sourcePlanes.front().get_height();
    const u32 depth = static_cast<u32>(sourcePlanes.size());
    u32 mipCount = 0u;
    if(!ComputeCompleteMipCount(TextureDimension::Texture3D, width, height, depth, mipCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: HDR volume dimensions cannot form a complete mip chain."));
        return false;
    }

    HdrVolumeMips mipVolumes;
    mipVolumes.resize(mipCount);
    mipVolumes[0u] = Move(sourcePlanes);
    for(u32 mipIndex = 1u; mipIndex < mipCount; ++mipIndex){
        if(!GenerateNextHdrVolumeMip(mipVolumes[mipIndex - 1u], mipVolumes[mipIndex]))
            return false;
    }
    return EncodeHdrMipChain(
        mipVolumes,
        TextureDimension::Texture3D,
        width,
        height,
        depth,
        outPayload
    );
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TEX_CONV_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

