// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"
#include "encode_backend.h"
#include "alpha_batches.h"

#include <core/common/log.h>

#include <global/simdmath.h>

#include <basisu_comp.h>
#include <basisu_enc.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TEX_CONV_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_encode{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using EncodeBackendDetail::ImagePlanes;
using EncodeBackendDetail::VolumeMips;
using EncodeBackendDetail::s_BasisEncoderWorkerCount;
using EncodeBackendDetail::s_BasisColorChannelMax;
using EncodeBackendDetail::s_BasisColorChannelRoundingBias;
using EncodeBackendDetail::s_HdrChannelCount;
using EncodeBackendDetail::s_BasisResampleBoxFilter;
using EncodeBackendDetail::s_BasisResampleFilterScale;
using EncodeBackendDetail::s_BasisResampleFilterChannelStart;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class BasisLibrary final{
public:
    BasisLibrary()noexcept = default;
    ~BasisLibrary(){
        if(m_initialized)
            basisu::basisu_encoder_deinit();
    }
    BasisLibrary(const BasisLibrary&) = delete;


public:
    BasisLibrary& operator=(const BasisLibrary&) = delete;


public:
    [[nodiscard]] bool initialize(){
        m_initialized = basisu::basisu_encoder_init(false);
        if(m_initialized)
            return true;

        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: failed to initialize the Basis Universal encoder."));
        return false;
    }


private:
    bool m_initialized = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ApplyAlphaSource(const AlphaSource& alphaSource, ImagePlanes& inOutPlanes){
    if(alphaSource.mode == AlphaSourceMode::Original)
        return true;
    if(inOutPlanes.empty())
        return false;

    const u32 width = inOutPlanes.front().get_width();
    const u32 height = inOutPlanes.front().get_height();
    for(const basisu::image& plane : inOutPlanes){
        if(plane.get_width() != width || plane.get_height() != height){
            NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: texture inputs have inconsistent resolutions."));
            return false;
        }
    }

    const auto alphaMask = EncodeBackendDetail::LoadAlphaMask(alphaSource, width, height);
    if(!alphaMask)
        return false;
    if(alphaSource.mode == AlphaSourceMode::Constant && !IsFinite(alphaSource.constant)){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: alpha constant must be finite."));
        return false;
    }

    if(alphaSource.mode != AlphaSourceMode::Constant && alphaSource.mode != AlphaSourceMode::Image){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: unsupported alpha source."));
        return false;
    }
    const SIMDVectorSingle saturatedConstantAlpha = VectorSaturate(VectorReplicate(alphaSource.constant));

    for(basisu::image& plane : inOutPlanes){
        for(u32 y = 0u; y < height; ++y){
            u32 x = 0u;
            const u32 chunkEndX = width & ~3u;
            for(; x < chunkEndX; x += 4u){
                SIMDVectorSingle alphaLanes = (alphaSource.mode == AlphaSourceMode::Constant) ? saturatedConstantAlpha : VectorSet((*alphaMask)(x, y)[0u], (*alphaMask)(x + 1u, y)[0u], (*alphaMask)(x + 2u, y)[0u], (*alphaMask)(x + 3u, y)[0u]);
                const SIMDVectorSingle quantizedLanes = VectorTruncate(VectorAdd(VectorMultiply(VectorSaturate(alphaLanes), VectorReplicate(s_BasisColorChannelMax)), VectorReplicate(s_BasisColorChannelRoundingBias)));
                plane(x, y).a = static_cast<u8>(VectorGetX(quantizedLanes));
                plane(x + 1u, y).a = static_cast<u8>(VectorGetY(quantizedLanes));
                plane(x + 2u, y).a = static_cast<u8>(VectorGetZ(quantizedLanes));
                plane(x + 3u, y).a = static_cast<u8>(VectorGetW(quantizedLanes));
            }
            const u32 remaining = width - x;
            if(remaining >= 2u){
                f32 values[4]{};
                for(u32 lane = 0u; lane < remaining; ++lane)
                    values[lane] = alphaSource.mode == AlphaSourceMode::Constant ? VectorGetX(saturatedConstantAlpha) : (*alphaMask)(x + lane, y)[0u];
                u8 quantized[4]{};
                if(AlphaBatchDetail::TryQuantize(values, remaining, quantized)){
                    for(u32 lane = 0u; lane < remaining; ++lane)
                        plane(x + lane, y).a = quantized[lane];
                    x += remaining;
                }
            }
            for(; x < width; ++x){
                const f32 alpha = (alphaSource.mode == AlphaSourceMode::Constant) ? VectorGetX(saturatedConstantAlpha) : (*alphaMask)(x, y)[0u];
                plane(x, y).a = static_cast<u8>(VectorGetX(VectorTruncate(VectorAdd(VectorMultiply(VectorSaturate(VectorReplicate(alpha)), VectorReplicate(s_BasisColorChannelMax)), VectorReplicate(s_BasisColorChannelRoundingBias)))));
            }
        }
    }
    return true;
}

[[nodiscard]] static Expected<TexturePayload> Encode2DOrCube(Core::Alloc::ScratchArena& scratchArena, const Span<const Path> inputPaths, const TextureDimension::Enum dimension, const bool srgb, const AlphaSource& alphaSource){
    TexturePayload payload;
    const u32 planeCount = dimension == TextureDimension::TextureCube ? TextureFormat::s_TextureCubeFaceCount : 1u;
    if(inputPaths.size() != planeCount)
        return MakeUnexpected(Failure{});

    basisu::job_pool jobPool(s_BasisEncoderWorkerCount);
    basisu::basis_compressor_params parameters;
    EncodeBackendDetail::ConfigureCompressor(parameters, jobPool, basist::basis_tex_format::cUASTC_LDR_4x4, srgb);
    parameters.m_tex_type = dimension == TextureDimension::TextureCube ? basist::cBASISTexTypeCubemapArray : basist::cBASISTexType2D;
    if(alphaSource.mode == AlphaSourceMode::Original){
        parameters.m_read_source_images = true;
        for(const Path& inputPath : inputPaths){
            const AString inputPathText = PathToGenericString<AString>(inputPath);
            parameters.m_source_filenames.push_back(AInteropString(inputPathText.data(), inputPathText.size()));
        }
    }
    else{
        auto sourcePlanes = EncodeBackendDetail::LoadPlanesFromFiles<EncodeBackendDetail::LdrPlaneLoader>(inputPaths);
        if(!sourcePlanes || !ApplyAlphaSource(alphaSource, *sourcePlanes))
            return MakeUnexpected(Failure{});
        parameters.m_read_source_images = false;
        parameters.m_source_images = Move(*sourcePlanes);
    }
    parameters.m_mip_gen = true;
    parameters.m_mip_smallest_dimension = 1u;
    parameters.m_mip_wrapping = false;

    basisu::basis_compressor compressor;
    if(!compressor.init(parameters)){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: Basis Universal failed to initialize the texture encoder."));
        return MakeUnexpected(Failure{});
    }
    const basisu::basis_compressor::error_code encodeResult = compressor.process();
    if(encodeResult != basisu::basis_compressor::cECSuccess){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: UASTC encoding failed (Basis Universal error {})."), static_cast<u32>(encodeResult));
        return MakeUnexpected(Failure{});
    }

    const basisu::basisu_backend_output& backendOutput = compressor.get_uastc_backend_output();
    if(!EncodeBackendDetail::ValidateBackendOutput(backendOutput, basist::basis_tex_format::cUASTC_LDR_4x4))
        return MakeUnexpected(Failure{});

    const basisu::basisu_backend_slice_desc& firstDescriptor = backendOutput.m_slice_desc.front();
    if(firstDescriptor.m_orig_width == 0u || firstDescriptor.m_orig_height == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: Basis Universal produced an invalid base resolution."));
        return MakeUnexpected(Failure{});
    }
    if(dimension == TextureDimension::TextureCube && firstDescriptor.m_orig_width != firstDescriptor.m_orig_height){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: cubemap faces must be square."));
        return MakeUnexpected(Failure{});
    }

    const auto mipCountResult = TextureFormat::ComputeCompleteMipCount(dimension, firstDescriptor.m_orig_width, firstDescriptor.m_orig_height, 1u);
    if(!mipCountResult){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: Basis Universal produced an invalid mip chain."));
        return MakeUnexpected(Failure{});
    }
    const u32 mipCount = *mipCountResult;
    const u64 expectedBackendSliceCount = static_cast<u64>(mipCount) * planeCount;
    if(backendOutput.m_slice_desc.size() != expectedBackendSliceCount){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: Basis Universal returned an incomplete UASTC mip chain."));
        return MakeUnexpected(Failure{});
    }

    EncodeBackendDetail::ResetPayload(payload, dimension, firstDescriptor.m_orig_width, firstDescriptor.m_orig_height, 1u, TexturePayloadFormat::UastcLdr4x4, srgb);
    payload.mips.reserve(mipCount);
    u32 mipWidth = payload.width;
    u32 mipHeight = payload.height;
    for(u32 mipIndex = 0u; mipIndex < mipCount; ++mipIndex){
        if(!EncodeBackendDetail::AppendCanonicalMip(scratchArena, backendOutput, mipIndex, planeCount, mipWidth, mipHeight, payload))
            return MakeUnexpected(Failure{});
        mipWidth = mipWidth > 1u ? mipWidth >> 1u : 1u;
        mipHeight = mipHeight > 1u ? mipHeight >> 1u : 1u;
    }

    payload.hasAlpha = compressor.get_any_source_image_has_alpha();
    return payload;
}

[[nodiscard]] static SIMDVectorSingle ConvertSrgbVolumeTexelToLinearRgb(const SIMDVectorSingle normalizedTexel)noexcept{
    const SIMDVectorSingle clamped = VectorSaturate(normalizedTexel);
    const SIMDVectorSingle scaled = VectorDivide(VectorAdd(clamped, VectorReplicate(0.055f)), VectorReplicate(1.055f));
    const SIMDVectorSingle nonlinear = VectorPow(scaled, VectorReplicate(2.4f));
    const SIMDVectorSingle linearPart = VectorDivide(clamped, VectorReplicate(12.92f));
    return VectorSelect(nonlinear, linearPart, VectorLess(clamped, VectorReplicate(0.04045f)));
}

[[nodiscard]] static SIMDVectorSingle ConvertLinearVolumeRgbToSrgb(const SIMDVectorSingle linearRgb)noexcept{
    const SIMDVectorSingle clamped = VectorSaturate(linearRgb);
    const SIMDVectorSingle nonlinear = VectorSubtract(VectorMultiply(VectorReplicate(1.055f), VectorPow(clamped, VectorReplicate(1.0f / 2.4f))), VectorReplicate(0.055f));
    const SIMDVectorSingle linearPart = VectorMultiply(clamped, VectorReplicate(12.92f));
    return VectorSaturate(VectorSelect(nonlinear, linearPart, VectorLess(clamped, VectorReplicate(0.0031308f))));
}

[[nodiscard]] static SIMDVectorSingle AverageLinearVolumeTexels(const SIMDVectorSingle channelSums, const u32 count)noexcept{
    NWB_ASSERT(count != 0u);
    const SIMDVectorSingle bias = VectorReplicate(static_cast<f32>(count) * 0.5f);
    const SIMDVectorSingle averaged = VectorDivide(VectorAdd(channelSums, bias), VectorReplicate(static_cast<f32>(count)));
    return VectorTruncate(averaged);
}

[[nodiscard]] static SIMDVectorSingle AverageSrgbVolumeTexels(const SIMDVectorSingle linearRgbSum, const SIMDVectorSingle alphaSum, const u32 count)noexcept{
    NWB_ASSERT(count != 0u);
    const SIMDVectorSingle averagedLinear = VectorDivide(linearRgbSum, VectorReplicate(static_cast<f32>(count)));
    const SIMDVectorSingle encodedRgb = ConvertLinearVolumeRgbToSrgb(averagedLinear);
    const SIMDVectorSingle scaledRgb = VectorMultiply(encodedRgb, VectorReplicate(s_BasisColorChannelMax));
    const SIMDVectorSingle roundedRgb = VectorTruncate(VectorAdd(scaledRgb, VectorReplicate(s_BasisColorChannelRoundingBias)));
    const SIMDVectorSingle alphaLane = VectorSelect(VectorZero(), alphaSum, s_SIMDMaskW);
    const SIMDVectorSingle averagedAlpha = VectorDivide(VectorAdd(alphaLane, VectorSelect(VectorZero(), VectorReplicate(static_cast<f32>(count) * 0.5f), s_SIMDMaskW)), VectorReplicate(static_cast<f32>(count)));
    return VectorSelect(roundedRgb, VectorTruncate(averagedAlpha), s_SIMDMaskW);
}

[[nodiscard]] static Expected<ImagePlanes> GenerateNextVolumeMip(const ImagePlanes& sourcePlanes, const bool srgb){
    return EncodeBackendDetail::GenerateVolumeMip(sourcePlanes,
        [srgb](const basisu::image& source, basisu::image& target){
            return basisu::image_resample(source, target, srgb, s_BasisResampleBoxFilter.data(), s_BasisResampleFilterScale, false, s_BasisResampleFilterChannelStart, s_HdrChannelCount);
        },
        [srgb](const ImagePlanes& filteredPlanes, basisu::image& targetPlane, const u32 x, const u32 y, const u32 filteredPlaneCount){
            SIMDVectorSingle channelSums = VectorZero();
            SIMDVectorSingle linearRgbSum = VectorZero();
            SIMDVectorSingle alphaSum = VectorZero();
            for(const basisu::image& filteredPlane : filteredPlanes){
                const basisu::color_rgba& sourceColor = filteredPlane(x, y);
                const SIMDVectorSingle texel = VectorSet(static_cast<f32>(sourceColor.r), static_cast<f32>(sourceColor.g), static_cast<f32>(sourceColor.b), static_cast<f32>(sourceColor.a));
                if(srgb){
                    const SIMDVectorSingle normalizedTexel = VectorDivide(texel, VectorReplicate(s_BasisColorChannelMax));
                    linearRgbSum = VectorAdd(linearRgbSum, ConvertSrgbVolumeTexelToLinearRgb(normalizedTexel));
                    alphaSum = VectorAdd(alphaSum, VectorSelect(VectorZero(), texel, s_SIMDMaskW));
                }
                else{
                    channelSums = VectorAdd(channelSums, texel);
                }
            }

            const SIMDVectorSingle average = srgb ? AverageSrgbVolumeTexels(linearRgbSum, alphaSum, filteredPlaneCount) : AverageLinearVolumeTexels(channelSums, filteredPlaneCount);
            Float4 targetTexel = {};
            StoreFloat(average, targetTexel);
            targetPlane(x, y) = basisu::color_rgba(static_cast<int>(targetTexel.r), static_cast<int>(targetTexel.g), static_cast<int>(targetTexel.b), static_cast<int>(targetTexel.a));
        }, NWB_TEXT("tex_conv: failed to generate a volume mip level."));
}

[[nodiscard]] static bool EncodeVolumeMip(Core::Alloc::ScratchArena& scratchArena, const ImagePlanes& planes, const bool srgb, TexturePayload& inOutPayload, bool& inOutHasAlpha){
    if(planes.empty() || planes.size() > Limit<u32>::s_Max)
        return false;

    const u32 width = planes.front().get_width();
    const u32 height = planes.front().get_height();
    for(const basisu::image& plane : planes){
        if(plane.get_width() != width || plane.get_height() != height){
            NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: generated volume mip planes have inconsistent dimensions."));
            return false;
        }
    }

    basisu::job_pool jobPool(s_BasisEncoderWorkerCount);
    basisu::basis_compressor_params parameters;
    EncodeBackendDetail::ConfigureCompressor(parameters, jobPool, basist::basis_tex_format::cUASTC_LDR_4x4, srgb);
    parameters.m_read_source_images = false;
    parameters.m_tex_type = basist::cBASISTexTypeVolume;
    parameters.m_source_images = planes;
    parameters.m_mip_gen = false;

    return EncodeBackendDetail::EncodeMip(scratchArena, parameters, basist::basis_tex_format::cUASTC_LDR_4x4, static_cast<u32>(planes.size()), width, height, inOutPayload, {
        NWB_TEXT("tex_conv: Basis Universal failed to initialize a volume mip encoder."),
        NWB_TEXT("tex_conv: UASTC volume mip encoding failed"),
        NWB_TEXT("tex_conv: Basis Universal returned an incomplete UASTC volume mip.")
    }, &inOutHasAlpha);
}

[[nodiscard]] static Expected<TexturePayload> EncodeVolume(Core::Alloc::ScratchArena& scratchArena, const Span<const Path> inputPaths, const bool srgb, const AlphaSource& alphaSource){
    TexturePayload payload;
    auto sourcePlanes = EncodeBackendDetail::LoadPlanesFromFiles<EncodeBackendDetail::LdrPlaneLoader>(inputPaths);
    if(!sourcePlanes || !ApplyAlphaSource(alphaSource, *sourcePlanes))
        return MakeUnexpected(Failure{});

    const u32 width = sourcePlanes->front().get_width();
    const u32 height = sourcePlanes->front().get_height();
    const u32 depth = static_cast<u32>(sourcePlanes->size());
    const auto mipCountResult = TextureFormat::ComputeCompleteMipCount(TextureDimension::Texture3D, width, height, depth);
    if(!mipCountResult){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: volume dimensions cannot form a complete mip chain."));
        return MakeUnexpected(Failure{});
    }

    const u32 mipCount = *mipCountResult;
    VolumeMips mipVolumes;
    mipVolumes.resize(mipCount);
    mipVolumes[0u] = Move(*sourcePlanes);
    for(u32 mipIndex = 1u; mipIndex < mipCount; ++mipIndex){
        auto mip = GenerateNextVolumeMip(mipVolumes[mipIndex - 1u], srgb);
        if(!mip)
            return MakeUnexpected(Failure{});
        mipVolumes[mipIndex] = Move(*mip);
    }

    EncodeBackendDetail::ResetPayload(payload, TextureDimension::Texture3D, width, height, depth, TexturePayloadFormat::UastcLdr4x4, srgb);
    payload.mips.reserve(mipCount);
    bool hasAlpha = false;
    for(u32 mipIndex = 0u; mipIndex < mipCount; ++mipIndex){
        if(!EncodeVolumeMip(scratchArena, mipVolumes[mipIndex], srgb, payload, hasAlpha))
            return MakeUnexpected(Failure{});
    }
    payload.hasAlpha = hasAlpha;
    return payload;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<TexturePayload> EncodeTexture(Core::Alloc::ScratchArena& scratchArena, const Span<const Path> inputPaths, const TextureDimension::Enum dimension, const bool srgb, const AlphaSource& alphaSource){
    if(inputPaths.empty())
        return MakeUnexpected(Failure{});

    const bool hdrInput = IsHdrInputPath(inputPaths.front());
    for(const Path& inputPath : inputPaths){
        if(IsHdrInputPath(inputPath) != hdrInput){
            NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: an individual texture conversion cannot mix HDR and LDR source images."));
            return MakeUnexpected(Failure{});
        }
    }

    __hidden_encode::BasisLibrary library;
    if(!library.initialize())
        return MakeUnexpected(Failure{});

    switch(dimension){
    case TextureDimension::Texture2D:
        if(inputPaths.size() != 1u){
            NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: a 2D texture requires exactly one input image."));
            return MakeUnexpected(Failure{});
        }
        return hdrInput ? EncodeBackendDetail::EncodeHdr2DOrCube(scratchArena, inputPaths, dimension, alphaSource) : __hidden_encode::Encode2DOrCube(scratchArena, inputPaths, dimension, srgb, alphaSource);
    case TextureDimension::TextureCube:
        if(inputPaths.size() != TextureFormat::s_TextureCubeFaceCount){
            NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: a cubemap requires exactly six ordered face images."));
            return MakeUnexpected(Failure{});
        }
        return hdrInput ? EncodeBackendDetail::EncodeHdr2DOrCube(scratchArena, inputPaths, dimension, alphaSource) : __hidden_encode::Encode2DOrCube(scratchArena, inputPaths, dimension, srgb, alphaSource);
    case TextureDimension::Texture3D:
        if(inputPaths.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: a volume texture requires one or more ordered Z slices."));
            return MakeUnexpected(Failure{});
        }
        return hdrInput ? EncodeBackendDetail::EncodeHdrVolume(scratchArena, inputPaths, alphaSource) : __hidden_encode::EncodeVolume(scratchArena, inputPaths, srgb, alphaSource);
    default:
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: unsupported texture dimension."));
        return MakeUnexpected(Failure{});
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TEX_CONV_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

