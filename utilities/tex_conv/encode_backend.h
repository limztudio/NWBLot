// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"

#include <core/common/log.h>

#include <basisu_comp.h>
#include <basisu_enc.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TEX_CONV_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace EncodeBackendDetail{


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
static constexpr StringView s_BasisResampleBoxFilter = "box";
static constexpr f32 s_BasisResampleFilterScale = 1.0f;
static constexpr u32 s_BasisResampleFilterChannelStart = 0u;

struct VolumeMipDims{
    u32 sourceWidth = 0u;
    u32 sourceHeight = 0u;
    u32 sourceDepth = 0u;
    u32 targetWidth = 0u;
    u32 targetHeight = 0u;
    u32 targetDepth = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ConfigureCompressor(basisu::basis_compressor_params& parameters, basisu::job_pool& jobPool, basist::basis_tex_format format, bool srgb);
void ResetPayload(TexturePayload& outPayload, const TextureDimension::Enum dimension, const u32 width, const u32 height, const u32 depth, const TexturePayloadFormat::Enum format, const bool srgb);
[[nodiscard]] bool ValidateBackendOutput(const basisu::basisu_backend_output& backendOutput, const basist::basis_tex_format expectedFormat);
[[nodiscard]] bool AppendCanonicalMip(const basisu::basisu_backend_output& backendOutput, const u32 backendMipIndex, const u32 planeCount, const u32 width, const u32 height, TexturePayload& inOutPayload);
[[nodiscard]] Expected<basisu::imagef> LoadAlphaMask(const AlphaSource& alphaSource, const u32 expectedWidth, const u32 expectedHeight);
[[nodiscard]] Expected<TexturePayload> EncodeHdr2DOrCube(const Vector<Path>& inputPaths, const TextureDimension::Enum dimension, const AlphaSource& alphaSource);
[[nodiscard]] Expected<TexturePayload> EncodeHdrVolume(const Vector<Path>& inputPaths, const AlphaSource& alphaSource);
[[nodiscard]] Expected<VolumeMipDims> ComputeVolumeMipDims(u32 sourceWidth, u32 sourceHeight, u32 sourceDepth)noexcept;
struct VolumeMipSliceRange{
    u32 first = 0u;
    u32 end = 0u;
};

[[nodiscard]] Expected<VolumeMipSliceRange> ComputeVolumeMipSliceRange(u32 sourceDepth, u32 targetDepth, u32 targetZ)noexcept;
struct LdrPlaneLoader{
    using Plane = basisu::image;
    static constexpr TStringView s_DecodeFailureLabel = NWB_TEXT("tex_conv: failed to decode input image '{}'.");
    static constexpr TStringView s_ResolutionFailureLabel = NWB_TEXT("tex_conv: input image '{}' has an invalid resolution.");
    static constexpr TStringView s_MismatchFailureLabel = NWB_TEXT("tex_conv: all LDR texture inputs must have the same resolution.");
    [[nodiscard]] static Expected<Plane> Decode(const AString& inputPathText){
        Plane plane;
        if(!basisu::load_image(inputPathText.c_str(), plane))
            return MakeUnexpected(Failure{});
        return plane;
    }
};
struct HdrPlaneLoader{
    using Plane = basisu::imagef;
    static constexpr TStringView s_DecodeFailureLabel = NWB_TEXT("tex_conv: failed to decode HDR image '{}'.");
    static constexpr TStringView s_ResolutionFailureLabel = NWB_TEXT("tex_conv: HDR image '{}' has an invalid resolution.");
    static constexpr TStringView s_MismatchFailureLabel = NWB_TEXT("tex_conv: all HDR texture inputs must have the same resolution.");
    [[nodiscard]] static Expected<Plane> Decode(const AString& inputPathText){
        Plane plane;
        if(!basisu::load_image_hdr(inputPathText.c_str(), plane, false))
            return MakeUnexpected(Failure{});
        return plane;
    }
};
template<typename PlaneLoader>
[[nodiscard]] Expected<basisu::vector<typename PlaneLoader::Plane>> LoadPlanesFromFiles(const Vector<Path>& inputPaths){
    using Plane = typename PlaneLoader::Plane;
    basisu::vector<Plane> planes;
    if(inputPaths.empty())
        return MakeUnexpected(Failure{});
    u32 width = 0u;
    u32 height = 0u;
    planes.reserve(inputPaths.size());
    for(const Path& inputPath : inputPaths){
        const AString inputPathText = PathToGenericString<AString>(inputPath);
        auto plane = PlaneLoader::Decode(inputPathText);
        if(!plane){
            NWB_LOGGER_ERROR(PlaneLoader::s_DecodeFailureLabel, PathToString<tchar>(inputPath));
            return MakeUnexpected(Failure{});
        }
        if(plane->get_width() == 0u || plane->get_height() == 0u){
            NWB_LOGGER_ERROR(PlaneLoader::s_ResolutionFailureLabel, PathToString<tchar>(inputPath));
            return MakeUnexpected(Failure{});
        }
        if(planes.empty()){
            width = plane->get_width();
            height = plane->get_height();
        }
        else if(plane->get_width() != width || plane->get_height() != height){
            NWB_LOGGER_ERROR(PlaneLoader::s_MismatchFailureLabel);
            return MakeUnexpected(Failure{});
        }
        planes.push_back(Move(*plane));
    }
    return planes;
}
template<typename PlaneVector>
struct VolumeMipTargets{
    PlaneVector planes;
    VolumeMipDims dims;
};

template<typename PlaneVector>
[[nodiscard]] Expected<VolumeMipTargets<PlaneVector>> PrepareVolumeMipTargets(const PlaneVector& sourcePlanes){
    if(sourcePlanes.empty() || sourcePlanes.size() > Limit<u32>::s_Max)
        return MakeUnexpected(Failure{});
    const auto dims = ComputeVolumeMipDims(sourcePlanes.front().get_width(), sourcePlanes.front().get_height(), static_cast<u32>(sourcePlanes.size()));
    if(!dims)
        return MakeUnexpected(Failure{});
    VolumeMipTargets<PlaneVector> targets;
    targets.dims = *dims;
    targets.planes.resize(dims->targetDepth);
    return targets;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TEX_CONV_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

