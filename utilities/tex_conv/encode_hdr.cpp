// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "encode_backend.h"
#include "alpha_batches.h"

#include <core/common/log.h>

#include <global/simdmath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TEX_CONV_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace EncodeBackendDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<basist::basis_texture_type> GetBasisTextureType(const TextureDimension::Enum dimension)noexcept{
    switch(dimension){
    case TextureDimension::Texture2D:
        return basist::cBASISTexType2D;
    case TextureDimension::TextureCube:
        return basist::cBASISTexTypeCubemapArray;
    case TextureDimension::Texture3D:
        return basist::cBASISTexTypeVolume;
    default:
        return MakeUnexpected(Failure{});
    }
}

[[nodiscard]] bool ValidHdrRgb(const SIMDVectorSingle rgb)noexcept{
    return Vector3IsFinite(rgb) && Vector3GreaterOrEqual(rgb, VectorZero()) && Vector3LessOrEqual(rgb, VectorReplicate(s_UastcHdrMaximum));
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

    const auto alphaMask = LoadAlphaMask(alphaSource, width, height);
    if(!alphaMask)
        return false;
    if(alphaSource.mode == AlphaSourceMode::Constant && !IsFinite(alphaSource.constant)){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: alpha constant must be finite."));
        return false;
    }

    if(alphaSource.mode != AlphaSourceMode::Original && alphaSource.mode != AlphaSourceMode::Constant && alphaSource.mode != AlphaSourceMode::Image){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: unsupported alpha source."));
        return false;
    }
    const SIMDVectorSingle saturatedConstantAlpha = VectorSaturate(VectorReplicate(alphaSource.constant));
    for(basisu::imagef& plane : inOutPlanes){
        for(u32 y = 0u; y < height; ++y){
            u32 x = 0u;
            const u32 chunkEndX = width & ~3u;
            for(; x < chunkEndX; x += 4u){
                if(alphaSource.mode == AlphaSourceMode::Original){
                    const f32 alpha0 = plane(x, y)[3u];
                    const f32 alpha1 = plane(x + 1u, y)[3u];
                    const f32 alpha2 = plane(x + 2u, y)[3u];
                    const f32 alpha3 = plane(x + 3u, y)[3u];
                    if(!IsFinite(alpha0) || !IsFinite(alpha1) || !IsFinite(alpha2) || !IsFinite(alpha3)){
                        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: HDR input contains a non-finite alpha value."));
                        return false;
                    }
                    const SIMDVectorSingle saturatedLanes = VectorSaturate(VectorSet(alpha0, alpha1, alpha2, alpha3));
                    plane(x, y)[3u] = VectorGetX(saturatedLanes);
                    plane(x + 1u, y)[3u] = VectorGetY(saturatedLanes);
                    plane(x + 2u, y)[3u] = VectorGetZ(saturatedLanes);
                    plane(x + 3u, y)[3u] = VectorGetW(saturatedLanes);
                }
                else if(alphaSource.mode == AlphaSourceMode::Constant){
                    plane(x, y)[3u] = VectorGetX(saturatedConstantAlpha);
                    plane(x + 1u, y)[3u] = VectorGetX(saturatedConstantAlpha);
                    plane(x + 2u, y)[3u] = VectorGetX(saturatedConstantAlpha);
                    plane(x + 3u, y)[3u] = VectorGetX(saturatedConstantAlpha);
                }
                else{
                    const SIMDVectorSingle saturatedLanes = VectorSaturate(VectorSet((*alphaMask)(x, y)[0u], (*alphaMask)(x + 1u, y)[0u], (*alphaMask)(x + 2u, y)[0u], (*alphaMask)(x + 3u, y)[0u]));
                    plane(x, y)[3u] = VectorGetX(saturatedLanes);
                    plane(x + 1u, y)[3u] = VectorGetY(saturatedLanes);
                    plane(x + 2u, y)[3u] = VectorGetZ(saturatedLanes);
                    plane(x + 3u, y)[3u] = VectorGetW(saturatedLanes);
                }
            }
            const u32 remaining = width - x;
            if(remaining >= 2u){
                f32 values[4]{};
                for(u32 lane = 0u; lane < remaining; ++lane){
                    if(alphaSource.mode == AlphaSourceMode::Original)
                        values[lane] = plane(x + lane, y)[3u];
                    else if(alphaSource.mode == AlphaSourceMode::Constant)
                        values[lane] = VectorGetX(saturatedConstantAlpha);
                    else
                        values[lane] = (*alphaMask)(x + lane, y)[0u];
                }
                f32 saturated[4]{};
                if(AlphaBatchDetail::TrySaturate(values, remaining, saturated)){
                    for(u32 lane = 0u; lane < remaining; ++lane)
                        plane(x + lane, y)[3u] = saturated[lane];
                    x += remaining;
                }
            }
            for(; x < width; ++x){
                f32 alpha = 1.0f;
                if(alphaSource.mode == AlphaSourceMode::Original){
                    alpha = plane(x, y)[3u];
                    if(!IsFinite(alpha)){
                        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: HDR input contains a non-finite alpha value."));
                        return false;
                    }
                }
                else if(alphaSource.mode == AlphaSourceMode::Constant)
                    alpha = VectorGetX(saturatedConstantAlpha);
                else
                    alpha = (*alphaMask)(x, y)[0u];
                plane(x, y)[3u] = VectorGetX(VectorSaturate(VectorReplicate(alpha)));
            }
        }
    }
    return true;
}

[[nodiscard]] Expected<HdrImagePlanes> GenerateNextHdrMip(const HdrImagePlanes& sourcePlanes){
    HdrImagePlanes planes;
    if(sourcePlanes.empty())
        return MakeUnexpected(Failure{});

    const u32 sourceWidth = sourcePlanes.front().get_width();
    const u32 sourceHeight = sourcePlanes.front().get_height();
    const u32 targetWidth = sourceWidth > 1u ? sourceWidth >> 1u : 1u;
    const u32 targetHeight = sourceHeight > 1u ? sourceHeight >> 1u : 1u;
    planes.resize(sourcePlanes.size());
    for(usize planeIndex = 0u; planeIndex < sourcePlanes.size(); ++planeIndex){
        const basisu::imagef& source = sourcePlanes[planeIndex];
        basisu::imagef& target = planes[planeIndex];
        target.resize(targetWidth, targetHeight);
        if(!basisu::image_resample(source, target, s_BasisResampleBoxFilter.data(), s_BasisResampleFilterScale, false, s_BasisResampleFilterChannelStart, s_HdrChannelCount)){
            NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: failed to generate an HDR mip level."));
            return MakeUnexpected(Failure{});
        }
    }
    return planes;
}

[[nodiscard]] Expected<HdrImagePlanes> GenerateNextHdrVolumeMip(const HdrImagePlanes& sourcePlanes){
    return EncodeBackendDetail::GenerateVolumeMip(sourcePlanes,
        [](const basisu::imagef& source, basisu::imagef& target){
            return basisu::image_resample(source, target, s_BasisResampleBoxFilter.data(), s_BasisResampleFilterScale, false, s_BasisResampleFilterChannelStart, s_HdrChannelCount);
        },
        [](const HdrImagePlanes& filteredPlanes, basisu::imagef& targetPlane, const u32 x, const u32 y, const u32 filteredPlaneCount){
            SIMDVectorSingle sum = VectorZero();
            for(const basisu::imagef& filteredPlane : filteredPlanes){
                const basisu::vec4F& sourceColor = filteredPlane(x, y);
                sum = VectorAdd(sum, VectorSet(sourceColor[0u], sourceColor[1u], sourceColor[2u], sourceColor[3u]));
            }

            const SIMDVectorSingle average = VectorScale(sum, 1.0f / static_cast<f32>(filteredPlaneCount));
            basisu::vec4F& targetColor = targetPlane(x, y);
            targetColor[0u] = VectorGetX(average);
            targetColor[1u] = VectorGetY(average);
            targetColor[2u] = VectorGetZ(average);
            targetColor[3u] = VectorGetW(average);
        }, NWB_TEXT("tex_conv: failed to generate an HDR volume mip level."));
}

struct HdrAlphaMips{
    VolumeMips mips;
    TextureAlphaMode::Enum mode = TextureAlphaMode::Opaque;
    u8 constantUnorm8 = TextureFormat::s_OpaqueAlphaUnorm8;
};

[[nodiscard]] Expected<HdrAlphaMips> ExtractHdrAlphaMips(HdrVolumeMips& inOutMipPlanes){
    HdrAlphaMips alpha;
    if(inOutMipPlanes.empty())
        return MakeUnexpected(Failure{});

    bool foundAlpha = false;
    bool allOpaque = true;
    bool allConstant = true;
    u8 constantAlpha = TextureFormat::s_OpaqueAlphaUnorm8;
    alpha.mips.resize(inOutMipPlanes.size());
    for(usize mipIndex = 0u; mipIndex < inOutMipPlanes.size(); ++mipIndex){
        HdrImagePlanes& hdrPlanes = inOutMipPlanes[mipIndex];
        if(!ValidateHdrRgbPlanes(hdrPlanes))
            return MakeUnexpected(Failure{});

        ImagePlanes& alphaPlanes = alpha.mips[mipIndex];
        alphaPlanes.resize(hdrPlanes.size());
        const u32 width = hdrPlanes.front().get_width();
        const u32 height = hdrPlanes.front().get_height();
        for(usize planeIndex = 0u; planeIndex < hdrPlanes.size(); ++planeIndex){
            basisu::imagef& hdrPlane = hdrPlanes[planeIndex];
            basisu::image& alphaPlane = alphaPlanes[planeIndex];
            alphaPlane.resize(width, height);
            for(u32 y = 0u; y < height; ++y){
                auto publish = [&](const u32 x, const u8 quantizedAlpha){
                    alphaPlane(x, y) = basisu::color_rgba(quantizedAlpha, quantizedAlpha, quantizedAlpha, TextureFormat::s_OpaqueAlphaUnorm8);
                    hdrPlane(x, y)[3u] = 1.0f;

                    if(!foundAlpha){
                        foundAlpha = true;
                        constantAlpha = quantizedAlpha;
                    }
                    else if(quantizedAlpha != constantAlpha)
                        allConstant = false;
                    if(quantizedAlpha != TextureFormat::s_OpaqueAlphaUnorm8)
                        allOpaque = false;
                };
                for(u32 x = 0u; x < width;){
                    const u32 count = width - x < 4u ? width - x : 4u;
                    f32 values[4]{};
                    for(u32 lane = 0u; lane < count; ++lane)
                        values[lane] = hdrPlane(x + lane, y)[3u];
                    u8 quantized[4]{};
                    if(AlphaBatchDetail::TryQuantize(values, count, quantized)){
                        for(u32 lane = 0u; lane < count; ++lane)
                            publish(x + lane, quantized[lane]);
                    }
                    else{
                        // A rejected packet retains the original finite-check, quantization and publication order up to its first error.
                        for(u32 lane = 0u; lane < count; ++lane){
                            const f32 alpha = hdrPlane(x + lane, y)[3u];
                            if(!IsFinite(alpha)){
                                NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: HDR mip generation produced a non-finite alpha value."));
                                return MakeUnexpected(Failure{});
                            }
                            const SIMDVectorSingle quantizedAlphaLanes = VectorTruncate(VectorAdd(VectorMultiply(VectorSaturate(VectorReplicate(alpha)), VectorReplicate(s_BasisColorChannelMax)), VectorReplicate(s_BasisColorChannelRoundingBias)));
                            publish(x + lane, static_cast<u8>(VectorGetX(quantizedAlphaLanes)));
                        }
                    }
                    x += count;
                }
            }
        }
    }
    if(!foundAlpha)
        return MakeUnexpected(Failure{});

    if(allOpaque){
        alpha.mode = TextureAlphaMode::Opaque;
        alpha.constantUnorm8 = TextureFormat::s_OpaqueAlphaUnorm8;
    }
    else if(allConstant){
        alpha.mode = TextureAlphaMode::ConstantUnorm8;
        alpha.constantUnorm8 = constantAlpha;
    }
    else{
        alpha.mode = TextureAlphaMode::SeparateUastcLdr4x4;
        alpha.constantUnorm8 = TextureFormat::s_OpaqueAlphaUnorm8;
    }
    return alpha;
}

[[nodiscard]] bool EncodeHdrMip(const HdrImagePlanes& planes, const TextureDimension::Enum dimension, TexturePayload& inOutPayload){
    if(!ValidateHdrRgbPlanes(planes))
        return false;

    const auto textureType = GetBasisTextureType(dimension);
    if(!textureType)
        return false;

    const u32 width = planes.front().get_width();
    const u32 height = planes.front().get_height();
    basisu::job_pool jobPool(s_BasisEncoderWorkerCount);
    basisu::basis_compressor_params parameters;
    EncodeBackendDetail::ConfigureCompressor(parameters, jobPool, basist::basis_tex_format::cUASTC_HDR_4x4, false);
    parameters.m_read_source_images = false;
    parameters.m_tex_type = *textureType;
    parameters.m_source_images_hdr = planes;
    parameters.m_mip_gen = false;

    return EncodeBackendDetail::EncodeMip(parameters, basist::basis_tex_format::cUASTC_HDR_4x4, static_cast<u32>(planes.size()), width, height, inOutPayload, {
        NWB_TEXT("tex_conv: Basis Universal failed to initialize an HDR mip encoder."),
        NWB_TEXT("tex_conv: UASTC HDR mip encoding failed"),
        NWB_TEXT("tex_conv: Basis Universal returned an incomplete UASTC HDR mip.")
    });
}

[[nodiscard]] bool EncodeHdrAlphaMip(const ImagePlanes& planes, const TextureDimension::Enum dimension, TexturePayload& inOutPayload){
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

    const auto textureType = GetBasisTextureType(dimension);
    if(!textureType)
        return false;

    basisu::job_pool jobPool(s_BasisEncoderWorkerCount);
    basisu::basis_compressor_params parameters;
    EncodeBackendDetail::ConfigureCompressor(parameters, jobPool, basist::basis_tex_format::cUASTC_LDR_4x4, false);
    parameters.m_read_source_images = false;
    parameters.m_tex_type = *textureType;
    parameters.m_source_images = planes;
    parameters.m_mip_gen = false;

    return EncodeBackendDetail::EncodeMip(parameters, basist::basis_tex_format::cUASTC_LDR_4x4, static_cast<u32>(planes.size()), width, height, inOutPayload, {
        NWB_TEXT("tex_conv: Basis Universal failed to initialize an HDR alpha mip encoder."),
        NWB_TEXT("tex_conv: UASTC HDR alpha mip encoding failed"),
        NWB_TEXT("tex_conv: Basis Universal returned an incomplete UASTC HDR alpha mip.")
    });
}

[[nodiscard]] bool ValidateHdrAlphaPayloadLayout(const TexturePayload& primaryPayload, const TexturePayload& alphaPayload){
    if(primaryPayload.bytes.size() != alphaPayload.bytes.size() || primaryPayload.mips.size() != alphaPayload.mips.size()){
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

[[nodiscard]] bool EncodeHdrAlphaMips(const VolumeMips& alphaMips, const TextureDimension::Enum dimension, const u32 width, const u32 height, const u32 depth, TexturePayload& inOutPayload){
    if(alphaMips.empty())
        return false;

    TexturePayload alphaPayload;
    ResetPayload(alphaPayload, dimension, width, height, depth, TexturePayloadFormat::UastcLdr4x4, false);
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

[[nodiscard]] Expected<TexturePayload> EncodeHdrMipChain(HdrVolumeMips& inOutMipPlanes, const TextureDimension::Enum dimension, const u32 width, const u32 height, const u32 depth){
    TexturePayload payload;
    if(inOutMipPlanes.empty())
        return MakeUnexpected(Failure{});

    const auto alpha = ExtractHdrAlphaMips(inOutMipPlanes);
    if(!alpha)
        return MakeUnexpected(Failure{});

    ResetPayload(payload, dimension, width, height, depth, TexturePayloadFormat::UastcHdr4x4, false);
    payload.mips.reserve(inOutMipPlanes.size());
    for(const HdrImagePlanes& mipPlanes : inOutMipPlanes){
        if(!EncodeHdrMip(mipPlanes, dimension, payload))
            return MakeUnexpected(Failure{});
    }

    payload.alphaMode = alpha->mode;
    payload.alphaConstantUnorm8 = alpha->constantUnorm8;
    payload.hasAlpha = alpha->mode != TextureAlphaMode::Opaque;
    if(alpha->mode == TextureAlphaMode::SeparateUastcLdr4x4){
        if(!EncodeHdrAlphaMips(alpha->mips, dimension, width, height, depth, payload))
            return MakeUnexpected(Failure{});
    }
    return payload;
}

[[nodiscard]] Expected<TexturePayload> EncodeHdr2DOrCube(const Vector<Path>& inputPaths, const TextureDimension::Enum dimension, const AlphaSource& alphaSource){
    const u32 planeCount = dimension == TextureDimension::TextureCube ? TextureFormat::s_TextureCubeFaceCount : 1u;
    if(inputPaths.size() != planeCount)
        return MakeUnexpected(Failure{});

    auto sourcePlanes = LoadPlanesFromFiles<HdrPlaneLoader>(inputPaths);
    if(!sourcePlanes || !ValidateHdrRgbPlanes(*sourcePlanes) || !ApplyHdrAlphaSource(alphaSource, *sourcePlanes))
        return MakeUnexpected(Failure{});
    const u32 width = sourcePlanes->front().get_width();
    const u32 height = sourcePlanes->front().get_height();
    if(dimension == TextureDimension::TextureCube && width != height){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: HDR cubemap faces must be square."));
        return MakeUnexpected(Failure{});
    }

    const auto mipCountResult = TextureFormat::ComputeCompleteMipCount(dimension, width, height, 1u);
    if(!mipCountResult){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: HDR texture dimensions cannot form a complete mip chain."));
        return MakeUnexpected(Failure{});
    }

    const u32 mipCount = *mipCountResult;
    HdrVolumeMips mipPlanes;
    mipPlanes.resize(mipCount);
    mipPlanes[0u] = Move(*sourcePlanes);
    for(u32 mipIndex = 1u; mipIndex < mipCount; ++mipIndex){
        auto mip = GenerateNextHdrMip(mipPlanes[mipIndex - 1u]);
        if(!mip)
            return MakeUnexpected(Failure{});
        mipPlanes[mipIndex] = Move(*mip);
    }
    return EncodeHdrMipChain(mipPlanes, dimension, width, height, 1u);
}

[[nodiscard]] Expected<TexturePayload> EncodeHdrVolume(const Vector<Path>& inputPaths, const AlphaSource& alphaSource){
    auto sourcePlanes = LoadPlanesFromFiles<HdrPlaneLoader>(inputPaths);
    if(!sourcePlanes || !ValidateHdrRgbPlanes(*sourcePlanes) || !ApplyHdrAlphaSource(alphaSource, *sourcePlanes))
        return MakeUnexpected(Failure{});

    const u32 width = sourcePlanes->front().get_width();
    const u32 height = sourcePlanes->front().get_height();
    const u32 depth = static_cast<u32>(sourcePlanes->size());
    const auto mipCountResult = TextureFormat::ComputeCompleteMipCount(TextureDimension::Texture3D, width, height, depth);
    if(!mipCountResult){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: HDR volume dimensions cannot form a complete mip chain."));
        return MakeUnexpected(Failure{});
    }

    const u32 mipCount = *mipCountResult;
    HdrVolumeMips mipVolumes;
    mipVolumes.resize(mipCount);
    mipVolumes[0u] = Move(*sourcePlanes);
    for(u32 mipIndex = 1u; mipIndex < mipCount; ++mipIndex){
        auto mip = GenerateNextHdrVolumeMip(mipVolumes[mipIndex - 1u]);
        if(!mip)
            return MakeUnexpected(Failure{});
        mipVolumes[mipIndex] = Move(*mip);
    }
    return EncodeHdrMipChain(mipVolumes, TextureDimension::Texture3D, width, height, depth);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TEX_CONV_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

