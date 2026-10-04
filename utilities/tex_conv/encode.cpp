// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"
#include "encode_backend.h"

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
    BasisLibrary() = default;
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

        NWB_LOGGER_ERROR(GLOBAL_TEXT("tex_conv: failed to initialize the Basis Universal encoder."));
        return false;
    }


private:
    bool m_initialized = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool LoadLdrPlanes(const Vector<Path>& inputPaths, ImagePlanes& outPlanes){
    return EncodeBackendDetail::LoadPlanesFromFiles<EncodeBackendDetail::LdrPlaneLoader>(inputPaths, outPlanes);
}

[[nodiscard]] static bool ApplyAlphaSource(const AlphaSource& alphaSource, ImagePlanes& inOutPlanes){
    if(alphaSource.mode == AlphaSourceMode::Original)
        return true;
    if(inOutPlanes.empty())
        return false;

    const u32 width = inOutPlanes.front().get_width();
    const u32 height = inOutPlanes.front().get_height();
    for(const basisu::image& plane : inOutPlanes){
        if(plane.get_width() != width || plane.get_height() != height){
            NWB_LOGGER_ERROR(GLOBAL_TEXT("tex_conv: texture inputs have inconsistent resolutions."));
            return false;
        }
    }

    basisu::imagef alphaMask;
    if(!EncodeBackendDetail::LoadAlphaMask(alphaSource, width, height, alphaMask))
        return false;
    if(alphaSource.mode == AlphaSourceMode::Constant && !IsFinite(alphaSource.constant)){
        NWB_LOGGER_ERROR(GLOBAL_TEXT("tex_conv: alpha constant must be finite."));
        return false;
    }

    if(alphaSource.mode != AlphaSourceMode::Constant && alphaSource.mode != AlphaSourceMode::Image){
        NWB_LOGGER_ERROR(GLOBAL_TEXT("tex_conv: unsupported alpha source."));
        return false;
    }
    const SIMDVector saturatedConstantAlpha = VectorSaturate(VectorReplicate(alphaSource.constant));

    for(basisu::image& plane : inOutPlanes){
        for(u32 y = 0u; y < height; ++y){
            u32 x = 0u;
            const u32 chunkEndX = width & ~3u;
            for(; x < chunkEndX; x += 4u){
                SIMDVector alphaLanes = (alphaSource.mode == AlphaSourceMode::Constant)
                    ? saturatedConstantAlpha
                    : VectorSet(alphaMask(x, y)[0u], alphaMask(x + 1u, y)[0u], alphaMask(x + 2u, y)[0u], alphaMask(x + 3u, y)[0u]);
                const SIMDVector quantizedLanes = VectorTruncate(VectorAdd(VectorMultiply(VectorSaturate(alphaLanes), VectorReplicate(s_BasisColorChannelMax)), VectorReplicate(s_BasisColorChannelRoundingBias)));
                plane(x, y).a = static_cast<u8>(VectorGetX(quantizedLanes));
                plane(x + 1u, y).a = static_cast<u8>(VectorGetY(quantizedLanes));
                plane(x + 2u, y).a = static_cast<u8>(VectorGetZ(quantizedLanes));
                plane(x + 3u, y).a = static_cast<u8>(VectorGetW(quantizedLanes));
            }
            for(; x < width; ++x){
                const f32 alpha = (alphaSource.mode == AlphaSourceMode::Constant)
                    ? VectorGetX(saturatedConstantAlpha)
                    : alphaMask(x, y)[0u];
                plane(x, y).a = static_cast<u8>(VectorGetX(VectorTruncate(VectorAdd(VectorMultiply(VectorSaturate(VectorReplicate(alpha)), VectorReplicate(s_BasisColorChannelMax)), VectorReplicate(s_BasisColorChannelRoundingBias)))));
            }
        }
    }
    return true;
}

[[nodiscard]] static bool Encode2DOrCube(
    const Vector<Path>& inputPaths,
    const TextureDimension::Enum dimension,
    const bool srgb,
    const AlphaSource& alphaSource,
    TexturePayload& outPayload
){
    const u32 planeCount = dimension == TextureDimension::TextureCube ? TextureFormat::s_TextureCubeFaceCount : 1u;
    if(inputPaths.size() != planeCount)
        return false;

    basisu::job_pool jobPool(s_BasisEncoderWorkerCount);
    basisu::basis_compressor_params parameters;
    parameters.set_format_mode(basist::basis_tex_format::cUASTC_LDR_4x4);
    parameters.set_srgb_options(srgb);
    parameters.m_tex_type = dimension == TextureDimension::TextureCube
        ? basist::cBASISTexTypeCubemapArray
        : basist::cBASISTexType2D
    ;
    if(alphaSource.mode == AlphaSourceMode::Original){
        parameters.m_read_source_images = true;
        for(const Path& inputPath : inputPaths){
            const AString inputPathText = PathToGenericString<AString>(inputPath);
            parameters.m_source_filenames.push_back(AInteropString(inputPathText.data(), inputPathText.size()));
        }
    }
    else{
        ImagePlanes sourcePlanes;
        if(!LoadLdrPlanes(inputPaths, sourcePlanes) || !ApplyAlphaSource(alphaSource, sourcePlanes))
            return false;
        parameters.m_read_source_images = false;
        parameters.m_source_images = Move(sourcePlanes);
    }
    parameters.m_mip_gen = true;
    parameters.m_mip_smallest_dimension = 1u;
    parameters.m_mip_wrapping = false;
    parameters.m_status_output = false;
    parameters.m_compute_stats = false;
    parameters.m_print_stats = false;
    parameters.m_write_output_basis_or_ktx2_files = false;
    parameters.m_create_ktx2_file = false;
    parameters.m_pJob_pool = &jobPool;

    basisu::basis_compressor compressor;
    if(!compressor.init(parameters)){
        NWB_LOGGER_ERROR(GLOBAL_TEXT("tex_conv: Basis Universal failed to initialize the texture encoder."));
        return false;
    }
    const basisu::basis_compressor::error_code encodeResult = compressor.process();
    if(encodeResult != basisu::basis_compressor::cECSuccess){
        NWB_LOGGER_ERROR(GLOBAL_TEXT("tex_conv: UASTC encoding failed (Basis Universal error {}).")
            , static_cast<u32>(encodeResult)
        );
        return false;
    }

    const basisu::basisu_backend_output& backendOutput = compressor.get_uastc_backend_output();
    if(!EncodeBackendDetail::ValidateBackendOutput(backendOutput, basist::basis_tex_format::cUASTC_LDR_4x4))
        return false;

    const basisu::basisu_backend_slice_desc& firstDescriptor = backendOutput.m_slice_desc.front();
    if(firstDescriptor.m_orig_width == 0u || firstDescriptor.m_orig_height == 0u){
        NWB_LOGGER_ERROR(GLOBAL_TEXT("tex_conv: Basis Universal produced an invalid base resolution."));
        return false;
    }
    if(dimension == TextureDimension::TextureCube && firstDescriptor.m_orig_width != firstDescriptor.m_orig_height){
        NWB_LOGGER_ERROR(GLOBAL_TEXT("tex_conv: cubemap faces must be square."));
        return false;
    }

    u32 mipCount = 0u;
    if(!TextureFormat::ComputeCompleteMipCount(dimension, firstDescriptor.m_orig_width, firstDescriptor.m_orig_height, 1u, mipCount)){
        NWB_LOGGER_ERROR(GLOBAL_TEXT("tex_conv: Basis Universal produced an invalid mip chain."));
        return false;
    }
    const u64 expectedBackendSliceCount = static_cast<u64>(mipCount) * planeCount;
    if(backendOutput.m_slice_desc.size() != expectedBackendSliceCount){
        NWB_LOGGER_ERROR(GLOBAL_TEXT("tex_conv: Basis Universal returned an incomplete UASTC mip chain."));
        return false;
    }

    EncodeBackendDetail::ResetPayload(
        outPayload,
        dimension,
        firstDescriptor.m_orig_width,
        firstDescriptor.m_orig_height,
        1u,
        TexturePayloadFormat::UastcLdr4x4,
        srgb
    );
    outPayload.mips.reserve(mipCount);
    u32 mipWidth = outPayload.width;
    u32 mipHeight = outPayload.height;
    for(u32 mipIndex = 0u; mipIndex < mipCount; ++mipIndex){
        if(!EncodeBackendDetail::AppendCanonicalMip(backendOutput, mipIndex, planeCount, mipWidth, mipHeight, outPayload))
            return false;
        mipWidth = mipWidth > 1u ? mipWidth >> 1u : 1u;
        mipHeight = mipHeight > 1u ? mipHeight >> 1u : 1u;
    }

    outPayload.hasAlpha = compressor.get_any_source_image_has_alpha();
    return true;
}

[[nodiscard]] static SIMDVector ConvertSrgbVolumeTexelToLinearRgb(const SIMDVector normalizedTexel){
    const SIMDVector clamped = VectorSaturate(normalizedTexel);
    const SIMDVector scaled = VectorDivide(VectorAdd(clamped, VectorReplicate(0.055f)), VectorReplicate(1.055f));
    const SIMDVector nonlinear = VectorPow(scaled, VectorReplicate(2.4f));
    const SIMDVector linearPart = VectorDivide(clamped, VectorReplicate(12.92f));
    return VectorSelect(nonlinear, linearPart, VectorLess(clamped, VectorReplicate(0.04045f)));
}

[[nodiscard]] static SIMDVector ConvertLinearVolumeRgbToSrgb(const SIMDVector linearRgb){
    const SIMDVector clamped = VectorSaturate(linearRgb);
    const SIMDVector nonlinear = VectorSubtract(VectorMultiply(VectorReplicate(1.055f), VectorPow(clamped, VectorReplicate(1.0f / 2.4f))), VectorReplicate(0.055f));
    const SIMDVector linearPart = VectorMultiply(clamped, VectorReplicate(12.92f));
    return VectorSaturate(VectorSelect(nonlinear, linearPart, VectorLess(clamped, VectorReplicate(0.0031308f))));
}

[[nodiscard]] static SIMDVector AverageLinearVolumeTexels(const SIMDVector channelSums, const u32 count){
    GLOBAL_ASSERT(count != 0u);
    const SIMDVector bias = VectorReplicate(static_cast<f32>(count) * 0.5f);
    const SIMDVector averaged = VectorDivide(VectorAdd(channelSums, bias), VectorReplicate(static_cast<f32>(count)));
    return VectorTruncate(averaged);
}

[[nodiscard]] static SIMDVector AverageSrgbVolumeTexels(
    const SIMDVector linearRgbSum,
    const SIMDVector alphaSum,
    const u32 count
){
    GLOBAL_ASSERT(count != 0u);
    const SIMDVector averagedLinear = VectorDivide(linearRgbSum, VectorReplicate(static_cast<f32>(count)));
    const SIMDVector encodedRgb = ConvertLinearVolumeRgbToSrgb(averagedLinear);
    const SIMDVector scaledRgb = VectorMultiply(encodedRgb, VectorReplicate(s_BasisColorChannelMax));
    const SIMDVector roundedRgb = VectorTruncate(VectorAdd(scaledRgb, VectorReplicate(s_BasisColorChannelRoundingBias)));
    const SIMDVector alphaLane = VectorSelect(VectorZero(), alphaSum, s_SIMDMaskW);
    const SIMDVector averagedAlpha = VectorDivide(VectorAdd(alphaLane, VectorSelect(VectorZero(), VectorReplicate(static_cast<f32>(count) * 0.5f), s_SIMDMaskW)), VectorReplicate(static_cast<f32>(count)));
    return VectorSelect(VectorSet(VectorGetX(roundedRgb), VectorGetY(roundedRgb), VectorGetZ(roundedRgb), 0.0f), VectorTruncate(averagedAlpha), s_SIMDMaskW);
}

[[nodiscard]] static bool GenerateNextVolumeMip(
    const ImagePlanes& sourcePlanes,
    const bool srgb,
    ImagePlanes& outPlanes
){
    EncodeBackendDetail::VolumeMipDims mipDims;
    if(!EncodeBackendDetail::PrepareVolumeMipTargets(
        sourcePlanes,
        sourcePlanes.front().get_width(),
        sourcePlanes.front().get_height(),
        outPlanes,
        mipDims
    ))
        return false;
    const u32 sourceDepth = mipDims.sourceDepth;
    const u32 targetWidth = mipDims.targetWidth;
    const u32 targetHeight = mipDims.targetHeight;
    const u32 targetDepth = mipDims.targetDepth;

    for(u32 targetZ = 0u; targetZ < targetDepth; ++targetZ){
        u32 sourceFirst = 0u;
        u32 sourceEnd = 0u;
        if(!EncodeBackendDetail::ComputeVolumeMipSliceRange(sourceDepth, targetDepth, targetZ, sourceFirst, sourceEnd))
            return false;

        basisu::vector<basisu::image> filteredPlanes;
        filteredPlanes.resize(sourceEnd - sourceFirst);
        for(u32 sourceZ = sourceFirst; sourceZ < sourceEnd; ++sourceZ){
            basisu::image& filteredPlane = filteredPlanes[sourceZ - sourceFirst];
            filteredPlane.resize(targetWidth, targetHeight);
            if(!basisu::image_resample(sourcePlanes[sourceZ], filteredPlane, srgb, s_BasisResampleBoxFilter.data(), s_BasisResampleFilterScale, false, s_BasisResampleFilterChannelStart, s_HdrChannelCount)){
                NWB_LOGGER_ERROR(GLOBAL_TEXT("tex_conv: failed to generate a volume mip level."));
                return false;
            }
        }

        const u32 filteredPlaneCount = static_cast<u32>(filteredPlanes.size());
        basisu::image& targetPlane = outPlanes[targetZ];
        targetPlane.resize(targetWidth, targetHeight);
        for(u32 y = 0u; y < targetHeight; ++y){
            for(u32 x = 0u; x < targetWidth; ++x){
                SIMDVector channelSums = VectorZero();
                SIMDVector linearRgbSum = VectorZero();
                SIMDVector alphaSum = VectorZero();
                for(const basisu::image& filteredPlane : filteredPlanes){
                    const basisu::color_rgba& sourceColor = filteredPlane(x, y);
                    const SIMDVector texel = VectorSet(static_cast<f32>(sourceColor.r), static_cast<f32>(sourceColor.g), static_cast<f32>(sourceColor.b), static_cast<f32>(sourceColor.a));
                    if(srgb){
                        const SIMDVector normalizedTexel = VectorDivide(texel, VectorReplicate(s_BasisColorChannelMax));
                        linearRgbSum = VectorAdd(linearRgbSum, ConvertSrgbVolumeTexelToLinearRgb(normalizedTexel));
                        alphaSum = VectorAdd(alphaSum, VectorSelect(VectorZero(), texel, s_SIMDMaskW));
                    }
                    else{
                        channelSums = VectorAdd(channelSums, texel);
                    }
                }

                const SIMDVector average = srgb
                    ? AverageSrgbVolumeTexels(linearRgbSum, alphaSum, filteredPlaneCount)
                    : AverageLinearVolumeTexels(channelSums, filteredPlaneCount)
                ;
                Float4 targetTexel = {};
                StoreFloat(average, targetTexel);
                targetPlane(x, y) = basisu::color_rgba(
                    static_cast<int>(targetTexel.r),
                    static_cast<int>(targetTexel.g),
                    static_cast<int>(targetTexel.b),
                    static_cast<int>(targetTexel.a)
                );
            }
        }
    }
    return true;
}

[[nodiscard]] static bool EncodeVolumeMip(
    const ImagePlanes& planes,
    const bool srgb,
    TexturePayload& inOutPayload,
    bool& inOutHasAlpha
){
    if(planes.empty() || planes.size() > Limit<u32>::s_Max)
        return false;

    const u32 width = planes.front().get_width();
    const u32 height = planes.front().get_height();
    for(const basisu::image& plane : planes){
        if(plane.get_width() != width || plane.get_height() != height){
            NWB_LOGGER_ERROR(GLOBAL_TEXT("tex_conv: generated volume mip planes have inconsistent dimensions."));
            return false;
        }
    }

    basisu::job_pool jobPool(s_BasisEncoderWorkerCount);
    basisu::basis_compressor_params parameters;
    parameters.set_format_mode(basist::basis_tex_format::cUASTC_LDR_4x4);
    parameters.set_srgb_options(srgb);
    parameters.m_read_source_images = false;
    parameters.m_tex_type = basist::cBASISTexTypeVolume;
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
        NWB_LOGGER_ERROR(GLOBAL_TEXT("tex_conv: Basis Universal failed to initialize a volume mip encoder."));
        return false;
    }
    const basisu::basis_compressor::error_code encodeResult = compressor.process();
    if(encodeResult != basisu::basis_compressor::cECSuccess){
        NWB_LOGGER_ERROR(GLOBAL_TEXT("tex_conv: UASTC volume mip encoding failed (Basis Universal error {}).")
            , static_cast<u32>(encodeResult)
        );
        return false;
    }

    const basisu::basisu_backend_output& backendOutput = compressor.get_uastc_backend_output();
    if(
        !EncodeBackendDetail::ValidateBackendOutput(backendOutput, basist::basis_tex_format::cUASTC_LDR_4x4)
        || backendOutput.m_slice_desc.size() != planes.size()
    ){
        NWB_LOGGER_ERROR(GLOBAL_TEXT("tex_conv: Basis Universal returned an incomplete UASTC volume mip."));
        return false;
    }
    if(!EncodeBackendDetail::AppendCanonicalMip(backendOutput, 0u, static_cast<u32>(planes.size()), width, height, inOutPayload))
        return false;

    inOutHasAlpha = inOutHasAlpha || compressor.get_any_source_image_has_alpha();
    return true;
}

[[nodiscard]] static bool EncodeVolume(
    const Vector<Path>& inputPaths,
    const bool srgb,
    const AlphaSource& alphaSource,
    TexturePayload& outPayload
){
    ImagePlanes sourcePlanes;
    if(!LoadLdrPlanes(inputPaths, sourcePlanes) || !ApplyAlphaSource(alphaSource, sourcePlanes))
        return false;

    const u32 width = sourcePlanes.front().get_width();
    const u32 height = sourcePlanes.front().get_height();
    const u32 depth = static_cast<u32>(sourcePlanes.size());
    u32 mipCount = 0u;
    if(!TextureFormat::ComputeCompleteMipCount(TextureDimension::Texture3D, width, height, depth, mipCount)){
        NWB_LOGGER_ERROR(GLOBAL_TEXT("tex_conv: volume dimensions cannot form a complete mip chain."));
        return false;
    }

    VolumeMips mipVolumes;
    mipVolumes.resize(mipCount);
    mipVolumes[0u] = Move(sourcePlanes);
    for(u32 mipIndex = 1u; mipIndex < mipCount; ++mipIndex){
        if(!GenerateNextVolumeMip(mipVolumes[mipIndex - 1u], srgb, mipVolumes[mipIndex]))
            return false;
    }

    EncodeBackendDetail::ResetPayload(
        outPayload,
        TextureDimension::Texture3D,
        width,
        height,
        depth,
        TexturePayloadFormat::UastcLdr4x4,
        srgb
    );
    outPayload.mips.reserve(mipCount);
    bool hasAlpha = false;
    for(u32 mipIndex = 0u; mipIndex < mipCount; ++mipIndex){
        if(!EncodeVolumeMip(mipVolumes[mipIndex], srgb, outPayload, hasAlpha))
            return false;
    }
    outPayload.hasAlpha = hasAlpha;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool EncodeTexture(
    const Vector<Path>& inputPaths,
    const TextureDimension::Enum dimension,
    const bool srgb,
    const AlphaSource& alphaSource,
    TexturePayload& outPayload
){
    if(inputPaths.empty())
        return false;

    const bool hdrInput = IsHdrInputPath(inputPaths.front());
    for(const Path& inputPath : inputPaths){
        if(IsHdrInputPath(inputPath) != hdrInput){
            NWB_LOGGER_ERROR(GLOBAL_TEXT("tex_conv: an individual texture conversion cannot mix HDR and LDR source images."));
            return false;
        }
    }

    __hidden_encode::BasisLibrary library;
    if(!library.initialize())
        return false;

    switch(dimension){
    case TextureDimension::Texture2D:
        if(inputPaths.size() != 1u){
            NWB_LOGGER_ERROR(GLOBAL_TEXT("tex_conv: a 2D texture requires exactly one input image."));
            return false;
        }
        return hdrInput
            ? EncodeBackendDetail::EncodeHdr2DOrCube(inputPaths, dimension, alphaSource, outPayload)
            : __hidden_encode::Encode2DOrCube(inputPaths, dimension, srgb, alphaSource, outPayload)
        ;
    case TextureDimension::TextureCube:
        if(inputPaths.size() != TextureFormat::s_TextureCubeFaceCount){
            NWB_LOGGER_ERROR(GLOBAL_TEXT("tex_conv: a cubemap requires exactly six ordered face images."));
            return false;
        }
        return hdrInput
            ? EncodeBackendDetail::EncodeHdr2DOrCube(inputPaths, dimension, alphaSource, outPayload)
            : __hidden_encode::Encode2DOrCube(inputPaths, dimension, srgb, alphaSource, outPayload)
        ;
    case TextureDimension::Texture3D:
        if(inputPaths.empty()){
            NWB_LOGGER_ERROR(GLOBAL_TEXT("tex_conv: a volume texture requires one or more ordered Z slices."));
            return false;
        }
        return hdrInput
            ? EncodeBackendDetail::EncodeHdrVolume(inputPaths, alphaSource, outPayload)
            : __hidden_encode::EncodeVolume(inputPaths, srgb, alphaSource, outPayload)
        ;
    default:
        NWB_LOGGER_ERROR(GLOBAL_TEXT("tex_conv: unsupported texture dimension."));
        return false;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TEX_CONV_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

