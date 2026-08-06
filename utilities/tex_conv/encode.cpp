// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"

#include <core/common/log.h>

#include <global/simdmath.h>

#include <basisu_comp.h>
#include <basisu_enc.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TEX_CONV_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_encode{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class BasisLibrary final{
public:
    BasisLibrary() = default;
    ~BasisLibrary(){
        if(m_initialized)
            basisu::basisu_encoder_deinit();
    }
    BasisLibrary(const BasisLibrary&) = delete;
    BasisLibrary& operator=(const BasisLibrary&) = delete;


public:
    [[nodiscard]] bool initialize(){
        m_initialized = basisu::basisu_encoder_init(false);
        if(m_initialized)
            return true;

        NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: failed to initialize the Basis Universal encoder."));
        return false;
    }


private:
    bool m_initialized = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using ImagePlanes = basisu::vector<basisu::image>;
using VolumeMips = basisu::vector<ImagePlanes>;
using HdrImagePlanes = basisu::vector<basisu::imagef>;
using HdrVolumeMips = basisu::vector<HdrImagePlanes>;

static constexpr usize s_InvalidBackendSlice = Limit<usize>::s_Max;
// Keep Basis encoding serial for reproducible cooked texture bytes across machines.
static constexpr u32 s_BasisEncoderWorkerCount = 1u;
// Basis image channels are normalized 8-bit values when averaging volume slices in linear space.
static constexpr f32 s_BasisColorChannelMax = 255.0f;
static constexpr f32 s_BasisColorChannelRoundingBias = 0.5f;
static constexpr f32 s_UastcHdrMaximum = 65216.0f;
static constexpr u32 s_HdrChannelCount = 4u;

static void ResetPayload(
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
    outPayload.alphaConstantUnorm8 = 255u;
    outPayload.mips.clear();
    outPayload.bytes.clear();
    outPayload.alphaBytes.clear();
}

[[nodiscard]] static bool ValidateBackendOutput(
    const basisu::basisu_backend_output& backendOutput,
    const basist::basis_tex_format expectedFormat
){
    if(backendOutput.m_tex_format != expectedFormat){
        NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: Basis Universal produced an unexpected UASTC block format."));
        return false;
    }
    if(backendOutput.m_slice_desc.empty() || backendOutput.m_slice_desc.size() != backendOutput.m_slice_image_data.size()){
        NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: Basis Universal returned an incomplete UASTC slice payload."));
        return false;
    }
    return true;
}

// Appends one raw mip in the on-disk canonical order: plane 0 through planeCount-1. Basis emits source-major slices,
// so this deliberately uses m_source_file_index rather than the physical backend ordering.
[[nodiscard]] static bool AppendCanonicalMip(
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
        NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: UASTC mip block layout exceeds supported limits."));
        return false;
    }
    if(planeByteCount > Limit<u64>::s_Max / planeCount){
        NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: UASTC mip payload size overflowed."));
        return false;
    }
    const u64 mipByteCount = planeByteCount * planeCount;
    if(mipByteCount > Limit<usize>::s_Max || inOutPayload.bytes.size() > Limit<usize>::s_Max - static_cast<usize>(mipByteCount)){
        NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: UASTC payload is too large to store."));
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
            NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: Basis Universal returned an unexpected texture-slice layout."));
            return false;
        }

        const basisu::uint8_vec& encodedBlocks = backendOutput.m_slice_image_data[backendSliceIndex];
        if(encodedBlocks.size_in_bytes() != planeByteCount){
            NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: Basis Universal produced an invalid UASTC block layout."));
            return false;
        }
        sliceForPlane[descriptor.m_source_file_index] = backendSliceIndex;
    }

    for(u32 planeIndex = 0u; planeIndex < planeCount; ++planeIndex){
        if(sliceForPlane[planeIndex] == s_InvalidBackendSlice){
            NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: Basis Universal omitted a required UASTC plane."));
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
            NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: failed to decode input image '{}'."), PathToString<tchar>(inputPath));
            return false;
        }
        if(plane.get_width() == 0u || plane.get_height() == 0u){
            NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: input image '{}' has an invalid resolution."), PathToString<tchar>(inputPath));
            return false;
        }
        if(outPlanes.empty()){
            width = plane.get_width();
            height = plane.get_height();
        }
        else if(plane.get_width() != width || plane.get_height() != height){
            NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: all LDR texture inputs must have the same resolution."));
            return false;
        }
        outPlanes.push_back(Move(plane));
    }
    return true;
}

[[nodiscard]] static bool LoadAlphaMask(
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
            NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: failed to decode alpha image '{}'."), PathToString<tchar>(alphaSource.path));
            return false;
        }
    }
    else{
        basisu::image sourceMask;
        if(!basisu::load_image(alphaPathText.c_str(), sourceMask)){
            NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: failed to decode alpha image '{}'."), PathToString<tchar>(alphaSource.path));
            return false;
        }
        outMask.resize(sourceMask.get_width(), sourceMask.get_height());
        for(u32 y = 0u; y < sourceMask.get_height(); ++y){
            for(u32 x = 0u; x < sourceMask.get_width(); ++x)
                outMask(x, y)[0u] = static_cast<f32>(sourceMask(x, y).r) / s_BasisColorChannelMax;
        }
    }
    if(outMask.get_width() != expectedWidth || outMask.get_height() != expectedHeight){
        NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: alpha image resolution must match the texture input resolution."));
        return false;
    }

    for(u32 y = 0u; y < expectedHeight; ++y){
        for(u32 x = 0u; x < expectedWidth; ++x){
            f32& alpha = outMask(x, y)[0u];
            if(!IsFinite(alpha)){
                NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: alpha image contains a non-finite red-channel value."));
                return false;
            }
            alpha = Saturate(alpha);
        }
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
            NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: texture inputs have inconsistent resolutions."));
            return false;
        }
    }

    basisu::imagef alphaMask;
    if(!LoadAlphaMask(alphaSource, width, height, alphaMask))
        return false;
    if(alphaSource.mode == AlphaSourceMode::Constant && !IsFinite(alphaSource.constant)){
        NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: alpha constant must be finite."));
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
                    NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: unsupported alpha source."));
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
        NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: Basis Universal failed to initialize the texture encoder."));
        return false;
    }
    const basisu::basis_compressor::error_code encodeResult = compressor.process();
    if(encodeResult != basisu::basis_compressor::cECSuccess){
        NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: UASTC encoding failed (Basis Universal error {})."), static_cast<u32>(encodeResult));
        return false;
    }

    const basisu::basisu_backend_output& backendOutput = compressor.get_uastc_backend_output();
    if(!ValidateBackendOutput(backendOutput, basist::basis_tex_format::cUASTC_LDR_4x4))
        return false;

    const basisu::basisu_backend_slice_desc& firstDescriptor = backendOutput.m_slice_desc.front();
    if(firstDescriptor.m_orig_width == 0u || firstDescriptor.m_orig_height == 0u){
        NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: Basis Universal produced an invalid base resolution."));
        return false;
    }
    if(dimension == TextureDimension::TextureCube && firstDescriptor.m_orig_width != firstDescriptor.m_orig_height){
        NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: cubemap faces must be square."));
        return false;
    }

    u32 mipCount = 0u;
    if(!ComputeCompleteMipCount(dimension, firstDescriptor.m_orig_width, firstDescriptor.m_orig_height, 1u, mipCount)){
        NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: Basis Universal produced an invalid mip chain."));
        return false;
    }
    const u64 expectedBackendSliceCount = static_cast<u64>(mipCount) * planeCount;
    if(backendOutput.m_slice_desc.size() != expectedBackendSliceCount){
        NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: Basis Universal returned an incomplete UASTC mip chain."));
        return false;
    }

    ResetPayload(
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
        if(!AppendCanonicalMip(backendOutput, mipIndex, planeCount, mipWidth, mipHeight, outPayload))
            return false;
        mipWidth = mipWidth > 1u ? mipWidth >> 1u : 1u;
        mipHeight = mipHeight > 1u ? mipHeight >> 1u : 1u;
    }

    outPayload.hasAlpha = compressor.get_any_source_image_has_alpha();
    return true;
}

[[nodiscard]] static basisu::color_rgba AverageVolumeTexels(
    const basisu::vector<basisu::image>& planes,
    const u32 x,
    const u32 y,
    const bool srgb
){
    const u32 count = static_cast<u32>(planes.size());
    u32 alpha = 0u;
    if(!srgb){
        u32 red = 0u;
        u32 green = 0u;
        u32 blue = 0u;
        for(const basisu::image& plane : planes){
            const basisu::color_rgba& color = plane(x, y);
            red += color.r;
            green += color.g;
            blue += color.b;
            alpha += color.a;
        }
        return basisu::color_rgba(
            static_cast<int>((red + count / 2u) / count),
            static_cast<int>((green + count / 2u) / count),
            static_cast<int>((blue + count / 2u) / count),
            static_cast<int>((alpha + count / 2u) / count)
        );
    }

    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
    for(const basisu::image& plane : planes){
        const basisu::color_rgba& color = plane(x, y);
        red += basisu::srgb_to_linear(static_cast<float>(color.r) / s_BasisColorChannelMax);
        green += basisu::srgb_to_linear(static_cast<float>(color.g) / s_BasisColorChannelMax);
        blue += basisu::srgb_to_linear(static_cast<float>(color.b) / s_BasisColorChannelMax);
        alpha += color.a;
    }
    return basisu::color_rgba(
        static_cast<int>(basisu::linear_to_srgb(red / count) * s_BasisColorChannelMax + s_BasisColorChannelRoundingBias),
        static_cast<int>(basisu::linear_to_srgb(green / count) * s_BasisColorChannelMax + s_BasisColorChannelRoundingBias),
        static_cast<int>(basisu::linear_to_srgb(blue / count) * s_BasisColorChannelMax + s_BasisColorChannelRoundingBias),
        static_cast<int>((alpha + count / 2u) / count)
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
                NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: failed to generate a volume mip level."));
                return false;
            }
        }

        basisu::image& targetPlane = outPlanes[targetZ];
        targetPlane.resize(targetWidth, targetHeight);
        for(u32 y = 0u; y < targetHeight; ++y){
            for(u32 x = 0u; x < targetWidth; ++x)
                targetPlane(x, y) = AverageVolumeTexels(filteredPlanes, x, y, srgb);
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
            NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: generated volume mip planes have inconsistent dimensions."));
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
        NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: Basis Universal failed to initialize a volume mip encoder."));
        return false;
    }
    const basisu::basis_compressor::error_code encodeResult = compressor.process();
    if(encodeResult != basisu::basis_compressor::cECSuccess){
        NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: UASTC volume mip encoding failed (Basis Universal error {})."), static_cast<u32>(encodeResult));
        return false;
    }

    const basisu::basisu_backend_output& backendOutput = compressor.get_uastc_backend_output();
    if(
        !ValidateBackendOutput(backendOutput, basist::basis_tex_format::cUASTC_LDR_4x4)
        || backendOutput.m_slice_desc.size() != planes.size()
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: Basis Universal returned an incomplete UASTC volume mip."));
        return false;
    }
    if(!AppendCanonicalMip(backendOutput, 0u, static_cast<u32>(planes.size()), width, height, inOutPayload))
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
        NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: volume dimensions cannot form a complete mip chain."));
        return false;
    }

    VolumeMips mipVolumes;
    mipVolumes.resize(mipCount);
    mipVolumes[0u] = Move(sourcePlanes);
    for(u32 mipIndex = 1u; mipIndex < mipCount; ++mipIndex){
        if(!GenerateNextVolumeMip(mipVolumes[mipIndex - 1u], srgb, mipVolumes[mipIndex]))
            return false;
    }

    ResetPayload(
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

[[nodiscard]] static bool GetBasisTextureType(
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

[[nodiscard]] static bool ValidateHdrRgbPlanes(const HdrImagePlanes& planes){
    if(planes.empty() || planes.size() > Limit<u32>::s_Max)
        return false;

    const u32 width = planes.front().get_width();
    const u32 height = planes.front().get_height();
    if(width == 0u || height == 0u){
        NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: HDR image has an invalid resolution."));
        return false;
    }
    for(const basisu::imagef& plane : planes){
        if(plane.get_width() != width || plane.get_height() != height){
            NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: HDR image planes have inconsistent dimensions."));
            return false;
        }
        for(u32 y = 0u; y < height; ++y){
            for(u32 x = 0u; x < width; ++x){
                const basisu::vec4F& color = plane(x, y);
                for(u32 channel = 0u; channel < 3u; ++channel){
                    const f32 value = color[channel];
                    if(!IsFinite(value) || value < 0.0f || value > s_UastcHdrMaximum){
                        NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: HDR RGB input must contain finite values in [0, 65216]."));
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

[[nodiscard]] static bool LoadHdrPlanes(const Vector<Path>& inputPaths, HdrImagePlanes& outPlanes){
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
            NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: failed to decode HDR image '{}'."), PathToString<tchar>(inputPath));
            return false;
        }
        if(plane.get_width() == 0u || plane.get_height() == 0u){
            NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: HDR image '{}' has an invalid resolution."), PathToString<tchar>(inputPath));
            return false;
        }
        if(outPlanes.empty()){
            width = plane.get_width();
            height = plane.get_height();
        }
        else if(plane.get_width() != width || plane.get_height() != height){
            NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: all HDR texture inputs must have the same resolution."));
            return false;
        }
        outPlanes.push_back(Move(plane));
    }
    return true;
}

[[nodiscard]] static bool ApplyHdrAlphaSource(const AlphaSource& alphaSource, HdrImagePlanes& inOutPlanes){
    if(inOutPlanes.empty())
        return false;

    const u32 width = inOutPlanes.front().get_width();
    const u32 height = inOutPlanes.front().get_height();
    for(const basisu::imagef& plane : inOutPlanes){
        if(plane.get_width() != width || plane.get_height() != height){
            NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: HDR texture inputs have inconsistent resolutions."));
            return false;
        }
    }

    basisu::imagef alphaMask;
    if(alphaSource.mode == AlphaSourceMode::Image && !LoadAlphaMask(alphaSource, width, height, alphaMask))
        return false;
    if(alphaSource.mode == AlphaSourceMode::Constant && !IsFinite(alphaSource.constant)){
        NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: alpha constant must be finite."));
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
                        NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: HDR input contains a non-finite alpha value."));
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
                    NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: unsupported alpha source."));
                    return false;
                }
                plane(x, y)[3u] = Saturate(alpha);
            }
        }
    }
    return true;
}

[[nodiscard]] static bool GenerateNextHdrMip(const HdrImagePlanes& sourcePlanes, HdrImagePlanes& outPlanes){
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
            NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: failed to generate an HDR mip level."));
            return false;
        }
    }
    return true;
}

static void AverageHdrVolumeTexels(const HdrImagePlanes& planes, const u32 x, const u32 y, basisu::vec4F& outColor){
    SIMDVector sum = VectorZero();
    for(const basisu::imagef& plane : planes){
        const basisu::vec4F& color = plane(x, y);
        sum = VectorAdd(sum, VectorSet(color[0u], color[1u], color[2u], color[3u]));
    }

    const SIMDVector average = VectorScale(sum, 1.0f / static_cast<f32>(planes.size()));
    outColor[0u] = VectorGetX(average);
    outColor[1u] = VectorGetY(average);
    outColor[2u] = VectorGetZ(average);
    outColor[3u] = VectorGetW(average);
}

[[nodiscard]] static bool GenerateNextHdrVolumeMip(const HdrImagePlanes& sourcePlanes, HdrImagePlanes& outPlanes){
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
                NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: failed to generate an HDR volume mip level."));
                return false;
            }
        }

        basisu::imagef& targetPlane = outPlanes[targetZ];
        targetPlane.resize(targetWidth, targetHeight);
        for(u32 y = 0u; y < targetHeight; ++y){
            for(u32 x = 0u; x < targetWidth; ++x)
                AverageHdrVolumeTexels(filteredPlanes, x, y, targetPlane(x, y));
        }
    }
    return true;
}

[[nodiscard]] static bool ExtractHdrAlphaMips(
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
    u8 constantAlpha = 255u;
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
                        NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: HDR mip generation produced a non-finite alpha value."));
                        return false;
                    }
                    const u8 quantizedAlpha = static_cast<u8>(
                        Saturate(alpha) * s_BasisColorChannelMax + s_BasisColorChannelRoundingBias
                    );
                    alphaPlane(x, y) = basisu::color_rgba(
                        quantizedAlpha,
                        quantizedAlpha,
                        quantizedAlpha,
                        255u
                    );
                    hdrColor[3u] = 1.0f;

                    if(!foundAlpha){
                        foundAlpha = true;
                        constantAlpha = quantizedAlpha;
                    }
                    else if(quantizedAlpha != constantAlpha)
                        allConstant = false;
                    if(quantizedAlpha != 255u)
                        allOpaque = false;
                }
            }
        }
    }
    if(!foundAlpha)
        return false;

    if(allOpaque){
        outAlphaMode = TextureAlphaMode::Opaque;
        outAlphaConstantUnorm8 = 255u;
    }
    else if(allConstant){
        outAlphaMode = TextureAlphaMode::ConstantUnorm8;
        outAlphaConstantUnorm8 = constantAlpha;
    }
    else{
        outAlphaMode = TextureAlphaMode::SeparateUastcLdr4x4;
        outAlphaConstantUnorm8 = 255u;
    }
    return true;
}

[[nodiscard]] static bool EncodeHdrMip(
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
        NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: Basis Universal failed to initialize an HDR mip encoder."));
        return false;
    }
    const basisu::basis_compressor::error_code encodeResult = compressor.process();
    if(encodeResult != basisu::basis_compressor::cECSuccess){
        NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: UASTC HDR mip encoding failed (Basis Universal error {})."), static_cast<u32>(encodeResult));
        return false;
    }

    const basisu::basisu_backend_output& backendOutput = compressor.get_uastc_backend_output();
    if(
        !ValidateBackendOutput(backendOutput, basist::basis_tex_format::cUASTC_HDR_4x4)
        || backendOutput.m_slice_desc.size() != planes.size()
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: Basis Universal returned an incomplete UASTC HDR mip."));
        return false;
    }
    return AppendCanonicalMip(backendOutput, 0u, static_cast<u32>(planes.size()), width, height, inOutPayload);
}

[[nodiscard]] static bool EncodeHdrAlphaMip(
    const ImagePlanes& planes,
    const TextureDimension::Enum dimension,
    TexturePayload& inOutPayload
){
    if(planes.empty() || planes.size() > Limit<u32>::s_Max)
        return false;

    const u32 width = planes.front().get_width();
    const u32 height = planes.front().get_height();
    if(width == 0u || height == 0u){
        NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: HDR alpha mip has an invalid resolution."));
        return false;
    }
    for(const basisu::image& plane : planes){
        if(plane.get_width() != width || plane.get_height() != height){
            NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: HDR alpha mip planes have inconsistent dimensions."));
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
        NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: Basis Universal failed to initialize an HDR alpha mip encoder."));
        return false;
    }
    const basisu::basis_compressor::error_code encodeResult = compressor.process();
    if(encodeResult != basisu::basis_compressor::cECSuccess){
        NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: UASTC HDR alpha mip encoding failed (Basis Universal error {})."), static_cast<u32>(encodeResult));
        return false;
    }

    const basisu::basisu_backend_output& backendOutput = compressor.get_uastc_backend_output();
    if(
        !ValidateBackendOutput(backendOutput, basist::basis_tex_format::cUASTC_LDR_4x4)
        || backendOutput.m_slice_desc.size() != planes.size()
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: Basis Universal returned an incomplete UASTC HDR alpha mip."));
        return false;
    }
    return AppendCanonicalMip(backendOutput, 0u, static_cast<u32>(planes.size()), width, height, inOutPayload);
}

[[nodiscard]] static bool ValidateHdrAlphaPayloadLayout(
    const TexturePayload& primaryPayload,
    const TexturePayload& alphaPayload
){
    if(
        primaryPayload.bytes.size() != alphaPayload.bytes.size()
        || primaryPayload.mips.size() != alphaPayload.mips.size()
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: UASTC HDR alpha payload does not match the RGB payload size."));
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
            NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: UASTC HDR alpha payload mip layout does not match RGB."));
            return false;
        }
    }
    return true;
}

[[nodiscard]] static bool EncodeHdrAlphaMips(
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

[[nodiscard]] static bool EncodeHdrMipChain(
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
    u8 alphaConstantUnorm8 = 255u;
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

[[nodiscard]] static bool EncodeHdr2DOrCube(
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
        NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: HDR cubemap faces must be square."));
        return false;
    }

    u32 mipCount = 0u;
    if(!ComputeCompleteMipCount(dimension, width, height, 1u, mipCount)){
        NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: HDR texture dimensions cannot form a complete mip chain."));
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

[[nodiscard]] static bool EncodeHdrVolume(
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
        NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: HDR volume dimensions cannot form a complete mip chain."));
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
            NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: an individual texture conversion cannot mix HDR and LDR source images."));
            return false;
        }
    }

    __hidden_encode::BasisLibrary library;
    if(!library.initialize())
        return false;

    switch(dimension){
    case TextureDimension::Texture2D:
        if(inputPaths.size() != 1u){
            NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: a 2D texture requires exactly one input image."));
            return false;
        }
        return hdrInput
            ? __hidden_encode::EncodeHdr2DOrCube(inputPaths, dimension, alphaSource, outPayload)
            : __hidden_encode::Encode2DOrCube(inputPaths, dimension, srgb, alphaSource, outPayload)
        ;
    case TextureDimension::TextureCube:
        if(inputPaths.size() != s_TextureCubeFaceCount){
            NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: a cubemap requires exactly six ordered face images."));
            return false;
        }
        return hdrInput
            ? __hidden_encode::EncodeHdr2DOrCube(inputPaths, dimension, alphaSource, outPayload)
            : __hidden_encode::Encode2DOrCube(inputPaths, dimension, srgb, alphaSource, outPayload)
        ;
    case TextureDimension::Texture3D:
        if(inputPaths.empty()){
            NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: a volume texture requires one or more ordered Z slices."));
            return false;
        }
        return hdrInput
            ? __hidden_encode::EncodeHdrVolume(inputPaths, alphaSource, outPayload)
            : __hidden_encode::EncodeVolume(inputPaths, srgb, alphaSource, outPayload)
        ;
    default:
        NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: unsupported texture dimension."));
        return false;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TEX_CONV_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

