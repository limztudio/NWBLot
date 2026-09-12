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
using EncodeBackendDetail::HdrImagePlanes;
using EncodeBackendDetail::HdrVolumeMips;
using EncodeBackendDetail::s_InvalidBackendSlice;
using EncodeBackendDetail::s_BasisEncoderWorkerCount;
using EncodeBackendDetail::s_BasisColorChannelMax;
using EncodeBackendDetail::s_BasisColorChannelRoundingBias;
using EncodeBackendDetail::s_UastcHdrMaximum;
using EncodeBackendDetail::s_HdrChannelCount;


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

        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: failed to initialize the Basis Universal encoder."));
        return false;
    }


private:
    bool m_initialized = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool LoadLdrPlanes(const Vector<Path>& inputPaths, ImagePlanes& outPlanes){
    outPlanes.clear();
    if(inputPaths.empty())
        return false;

    u32 width = 0u;
    u32 height = 0u;
    outPlanes.reserve(inputPaths.size());
    for(const Path& inputPath : inputPaths){
        const AString inputPathText = PathToGenericString<AString>(inputPath);
        basisu::image plane;
        if(!basisu::load_image(inputPathText.c_str(), plane)){
            NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: failed to decode input image '{}'."), PathToString<tchar>(inputPath));
            return false;
        }
        if(plane.get_width() == 0u || plane.get_height() == 0u){
            NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: input image '{}' has an invalid resolution."), PathToString<tchar>(inputPath));
            return false;
        }
        if(outPlanes.empty()){
            width = plane.get_width();
            height = plane.get_height();
        }
        else if(plane.get_width() != width || plane.get_height() != height){
            NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: all LDR texture inputs must have the same resolution."));
            return false;
        }
        outPlanes.push_back(Move(plane));
    }
    return true;
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
            NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: texture inputs have inconsistent resolutions."));
            return false;
        }
    }

    basisu::imagef alphaMask;
    if(!EncodeBackendDetail::LoadAlphaMask(alphaSource, width, height, alphaMask))
        return false;
    if(alphaSource.mode == AlphaSourceMode::Constant && !IsFinite(alphaSource.constant)){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: alpha constant must be finite."));
        return false;
    }

    for(basisu::image& plane : inOutPlanes){
        for(u32 y = 0u; y < height; ++y){
            for(u32 x = 0u; x < width; ++x){
                f32 alpha = 1.0f;
                switch(alphaSource.mode){
                case AlphaSourceMode::Constant:
                    alpha = Saturate(alphaSource.constant);
                    break;
                case AlphaSourceMode::Image:
                    alpha = alphaMask(x, y)[0u];
                    break;
                default:
                    NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: unsupported alpha source."));
                    return false;
                }
                plane(x, y).a = static_cast<u8>(alpha * s_BasisColorChannelMax + s_BasisColorChannelRoundingBias);
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
    const u32 planeCount = dimension == TextureDimension::TextureCube ? s_TextureCubeFaceCount : 1u;
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
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: Basis Universal failed to initialize the texture encoder."));
        return false;
    }
    const basisu::basis_compressor::error_code encodeResult = compressor.process();
    if(encodeResult != basisu::basis_compressor::cECSuccess){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: UASTC encoding failed (Basis Universal error {}).")
            , static_cast<u32>(encodeResult)
        );
        return false;
    }

    const basisu::basisu_backend_output& backendOutput = compressor.get_uastc_backend_output();
    if(!EncodeBackendDetail::ValidateBackendOutput(backendOutput, basist::basis_tex_format::cUASTC_LDR_4x4))
        return false;

    const basisu::basisu_backend_slice_desc& firstDescriptor = backendOutput.m_slice_desc.front();
    if(firstDescriptor.m_orig_width == 0u || firstDescriptor.m_orig_height == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: Basis Universal produced an invalid base resolution."));
        return false;
    }
    if(dimension == TextureDimension::TextureCube && firstDescriptor.m_orig_width != firstDescriptor.m_orig_height){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: cubemap faces must be square."));
        return false;
    }

    u32 mipCount = 0u;
    if(!ComputeCompleteMipCount(dimension, firstDescriptor.m_orig_width, firstDescriptor.m_orig_height, 1u, mipCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: Basis Universal produced an invalid mip chain."));
        return false;
    }
    const u64 expectedBackendSliceCount = static_cast<u64>(mipCount) * planeCount;
    if(backendOutput.m_slice_desc.size() != expectedBackendSliceCount){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: Basis Universal returned an incomplete UASTC mip chain."));
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

[[nodiscard]] static SIMDVector AverageLinearVolumeTexels(const SIMDVector channelSums, const u32 count){
    NWB_ASSERT(count != 0u);
    const u32 rounding = count / 2u;
    return VectorSetInt(
        (VectorGetIntX(channelSums) + rounding) / count,
        (VectorGetIntY(channelSums) + rounding) / count,
        (VectorGetIntZ(channelSums) + rounding) / count,
        (VectorGetIntW(channelSums) + rounding) / count
    );
}

[[nodiscard]] static SIMDVector ConvertSrgbVolumeTexelToLinearRgb(const SIMDVector texel){
    return VectorSet(
        basisu::srgb_to_linear(static_cast<f32>(VectorGetIntX(texel)) / s_BasisColorChannelMax),
        basisu::srgb_to_linear(static_cast<f32>(VectorGetIntY(texel)) / s_BasisColorChannelMax),
        basisu::srgb_to_linear(static_cast<f32>(VectorGetIntZ(texel)) / s_BasisColorChannelMax),
        0.0f
    );
}

[[nodiscard]] static SIMDVector AverageSrgbVolumeTexels(
    const SIMDVector linearRgbSum,
    const SIMDVector alphaSum,
    const u32 count
){
    NWB_ASSERT(count != 0u);
    const f32 floatCount = static_cast<f32>(count);
    const u32 rounding = count / 2u;
    return VectorSetInt(
        static_cast<u32>(basisu::linear_to_srgb(VectorGetX(linearRgbSum) / floatCount) * s_BasisColorChannelMax + s_BasisColorChannelRoundingBias),
        static_cast<u32>(basisu::linear_to_srgb(VectorGetY(linearRgbSum) / floatCount) * s_BasisColorChannelMax + s_BasisColorChannelRoundingBias),
        static_cast<u32>(basisu::linear_to_srgb(VectorGetZ(linearRgbSum) / floatCount) * s_BasisColorChannelMax + s_BasisColorChannelRoundingBias),
        (VectorGetIntW(alphaSum) + rounding) / count
    );
}

[[nodiscard]] static bool GenerateNextVolumeMip(
    const ImagePlanes& sourcePlanes,
    const bool srgb,
    ImagePlanes& outPlanes
){
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

        basisu::vector<basisu::image> filteredPlanes;
        filteredPlanes.resize(sourceEnd - sourceFirst);
        for(u32 sourceZ = sourceFirst; sourceZ < sourceEnd; ++sourceZ){
            basisu::image& filteredPlane = filteredPlanes[sourceZ - sourceFirst];
            filteredPlane.resize(targetWidth, targetHeight);
            if(!basisu::image_resample(sourcePlanes[sourceZ], filteredPlane, srgb, "box", 1.0f, false, 0u, 4u)){
                NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: failed to generate a volume mip level."));
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
                    UInt4 sourceTexel = {};
                    sourceTexel.r = static_cast<u32>(sourceColor.r);
                    sourceTexel.g = static_cast<u32>(sourceColor.g);
                    sourceTexel.b = static_cast<u32>(sourceColor.b);
                    sourceTexel.a = static_cast<u32>(sourceColor.a);
                    const SIMDVector texel = LoadInt(sourceTexel);
                    if(srgb){
                        linearRgbSum = VectorAdd(linearRgbSum, ConvertSrgbVolumeTexelToLinearRgb(texel));
                        alphaSum = VectorAddInt(alphaSum, VectorAndInt(texel, s_SIMDMaskW));
                    }
                    else{
                        channelSums = VectorAddInt(channelSums, texel);
                    }
                }

                const SIMDVector average = srgb
                    ? AverageSrgbVolumeTexels(linearRgbSum, alphaSum, filteredPlaneCount)
                    : AverageLinearVolumeTexels(channelSums, filteredPlaneCount)
                ;
                UInt4 targetTexel = {};
                StoreInt(average, targetTexel);
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
            NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: generated volume mip planes have inconsistent dimensions."));
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
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: Basis Universal failed to initialize a volume mip encoder."));
        return false;
    }
    const basisu::basis_compressor::error_code encodeResult = compressor.process();
    if(encodeResult != basisu::basis_compressor::cECSuccess){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: UASTC volume mip encoding failed (Basis Universal error {}).")
            , static_cast<u32>(encodeResult)
        );
        return false;
    }

    const basisu::basisu_backend_output& backendOutput = compressor.get_uastc_backend_output();
    if(
        !EncodeBackendDetail::ValidateBackendOutput(backendOutput, basist::basis_tex_format::cUASTC_LDR_4x4)
        || backendOutput.m_slice_desc.size() != planes.size()
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: Basis Universal returned an incomplete UASTC volume mip."));
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
    if(!ComputeCompleteMipCount(TextureDimension::Texture3D, width, height, depth, mipCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: volume dimensions cannot form a complete mip chain."));
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
            NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: an individual texture conversion cannot mix HDR and LDR source images."));
            return false;
        }
    }

    __hidden_encode::BasisLibrary library;
    if(!library.initialize())
        return false;

    switch(dimension){
    case TextureDimension::Texture2D:
        if(inputPaths.size() != 1u){
            NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: a 2D texture requires exactly one input image."));
            return false;
        }
        return hdrInput
            ? EncodeBackendDetail::EncodeHdr2DOrCube(inputPaths, dimension, alphaSource, outPayload)
            : __hidden_encode::Encode2DOrCube(inputPaths, dimension, srgb, alphaSource, outPayload)
        ;
    case TextureDimension::TextureCube:
        if(inputPaths.size() != s_TextureCubeFaceCount){
            NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: a cubemap requires exactly six ordered face images."));
            return false;
        }
        return hdrInput
            ? EncodeBackendDetail::EncodeHdr2DOrCube(inputPaths, dimension, alphaSource, outPayload)
            : __hidden_encode::Encode2DOrCube(inputPaths, dimension, srgb, alphaSource, outPayload)
        ;
    case TextureDimension::Texture3D:
        if(inputPaths.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: a volume texture requires one or more ordered Z slices."));
            return false;
        }
        return hdrInput
            ? EncodeBackendDetail::EncodeHdrVolume(inputPaths, alphaSource, outPayload)
            : __hidden_encode::EncodeVolume(inputPaths, srgb, alphaSource, outPayload)
        ;
    default:
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: unsupported texture dimension."));
        return false;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TEX_CONV_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
