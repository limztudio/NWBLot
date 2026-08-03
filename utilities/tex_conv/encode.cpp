// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"

#include <core/common/log.h>

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

static constexpr usize s_InvalidBackendSlice = Limit<usize>::s_Max;
// Keep Basis encoding serial for reproducible cooked texture bytes across machines.
static constexpr u32 s_BasisEncoderWorkerCount = 1u;
// Basis image channels are normalized 8-bit values when averaging volume slices in linear space.
static constexpr f32 s_BasisColorChannelMax = 255.0f;
static constexpr f32 s_BasisColorChannelRoundingBias = 0.5f;

static void ResetPayload(
    TexturePayload& outPayload,
    const TextureDimension::Enum dimension,
    const u32 width,
    const u32 height,
    const u32 depth
){
    outPayload.dimension = dimension;
    outPayload.width = width;
    outPayload.height = height;
    outPayload.depth = depth;
    outPayload.hasAlpha = false;
    outPayload.mips.clear();
    outPayload.bytes.clear();
}

[[nodiscard]] static bool ValidateBackendOutput(const basisu::basisu_backend_output& backendOutput){
    if(backendOutput.m_tex_format != basist::basis_tex_format::cUASTC_LDR_4x4){
        NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: Basis Universal did not produce UASTC LDR 4x4 blocks."));
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
    if(!ComputePlaneBlockLayout(width, height, blocksX, blocksY, planeByteCount) || planeByteCount > Limit<usize>::s_Max){
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

[[nodiscard]] static bool Encode2DOrCube(
    const Vector<Path>& inputPaths,
    const TextureDimension::Enum dimension,
    const bool srgb,
    TexturePayload& outPayload
){
    const u32 planeCount = dimension == TextureDimension::TextureCube ? s_TextureCubeFaceCount : 1u;
    if(inputPaths.size() != planeCount)
        return false;

    basisu::job_pool jobPool(s_BasisEncoderWorkerCount);
    basisu::basis_compressor_params parameters;
    parameters.set_format_mode(basist::basis_tex_format::cUASTC_LDR_4x4);
    parameters.set_srgb_options(srgb);
    parameters.m_read_source_images = true;
    parameters.m_tex_type = dimension == TextureDimension::TextureCube
        ? basist::cBASISTexTypeCubemapArray
        : basist::cBASISTexType2D
    ;
    for(const Path& inputPath : inputPaths){
        const AString inputPathText = PathToUtf8(inputPath);
        parameters.m_source_filenames.push_back(AInteropString(inputPathText.data(), inputPathText.size()));
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
    if(!ValidateBackendOutput(backendOutput))
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

    ResetPayload(outPayload, dimension, firstDescriptor.m_orig_width, firstDescriptor.m_orig_height, 1u);
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

[[nodiscard]] static bool LoadVolumePlanes(const Vector<Path>& inputPaths, ImagePlanes& outPlanes){
    outPlanes.clear();
    if(inputPaths.empty())
        return false;

    u32 width = 0u;
    u32 height = 0u;
    outPlanes.reserve(inputPaths.size());
    for(const Path& inputPath : inputPaths){
        const AString inputPathText = PathToUtf8(inputPath);
        basisu::image plane;
        if(!basisu::load_image(inputPathText.c_str(), plane)){
            NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: failed to decode volume slice '{}'."), PathToString<tchar>(inputPath));
            return false;
        }
        if(plane.get_width() == 0u || plane.get_height() == 0u){
            NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: volume slice '{}' has an invalid resolution."), PathToString<tchar>(inputPath));
            return false;
        }
        if(outPlanes.empty()){
            width = plane.get_width();
            height = plane.get_height();
        }
        else if(plane.get_width() != width || plane.get_height() != height){
            NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: all volume slices must have the same resolution."));
            return false;
        }
        outPlanes.push_back(Move(plane));
    }
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
    if(!ValidateBackendOutput(backendOutput) || backendOutput.m_slice_desc.size() != planes.size()){
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
    TexturePayload& outPayload
){
    ImagePlanes sourcePlanes;
    if(!LoadVolumePlanes(inputPaths, sourcePlanes))
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

    ResetPayload(outPayload, TextureDimension::Texture3D, width, height, depth);
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
    TexturePayload& outPayload
){
    __hidden_encode::BasisLibrary library;
    if(!library.initialize())
        return false;

    switch(dimension){
    case TextureDimension::Texture2D:
        if(inputPaths.size() != 1u){
            NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: a 2D texture requires exactly one input image."));
            return false;
        }
        return __hidden_encode::Encode2DOrCube(inputPaths, dimension, srgb, outPayload);
    case TextureDimension::TextureCube:
        if(inputPaths.size() != s_TextureCubeFaceCount){
            NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: a cubemap requires exactly six ordered face images."));
            return false;
        }
        return __hidden_encode::Encode2DOrCube(inputPaths, dimension, srgb, outPayload);
    case TextureDimension::Texture3D:
        if(inputPaths.empty()){
            NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: a volume texture requires one or more ordered Z slices."));
            return false;
        }
        return __hidden_encode::EncodeVolume(inputPaths, srgb, outPayload);
    default:
        NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: unsupported texture dimension."));
        return false;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TEX_CONV_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
