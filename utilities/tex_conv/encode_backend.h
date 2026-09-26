// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"

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
static constexpr const char* s_BasisResampleBoxFilter = "box";
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


void ResetPayload(TexturePayload& outPayload, const TextureDimension::Enum dimension, const u32 width, const u32 height, const u32 depth, const TexturePayloadFormat::Enum format, const bool srgb);
[[nodiscard]] bool ValidateBackendOutput(const basisu::basisu_backend_output& backendOutput, const basist::basis_tex_format expectedFormat);
[[nodiscard]] bool AppendCanonicalMip(const basisu::basisu_backend_output& backendOutput, const u32 backendMipIndex, const u32 planeCount, const u32 width, const u32 height, TexturePayload& inOutPayload);
[[nodiscard]] bool LoadAlphaMask(const AlphaSource& alphaSource, const u32 expectedWidth, const u32 expectedHeight, basisu::imagef& outMask);
[[nodiscard]] bool EncodeHdr2DOrCube(const Vector<Path>& inputPaths, const TextureDimension::Enum dimension, const AlphaSource& alphaSource, TexturePayload& outPayload);
[[nodiscard]] bool EncodeHdrVolume(const Vector<Path>& inputPaths, const AlphaSource& alphaSource, TexturePayload& outPayload);
[[nodiscard]] bool ComputeVolumeMipDims(u32 sourceWidth, u32 sourceHeight, u32 sourceDepth, VolumeMipDims& outDims);
[[nodiscard]] bool ComputeVolumeMipSliceRange(u32 sourceDepth, u32 targetDepth, u32 targetZ, u32& outFirst, u32& outEnd);
// Shared plane-loader policies: LDR decodes 8-bit images, HDR decodes float images.
struct LdrPlaneLoader{
    using Plane = basisu::image;
    static constexpr AStringView s_DecodeFailureLabel = "tex_conv: failed to decode input image '{}'.";
    static constexpr AStringView s_ResolutionFailureLabel = "tex_conv: input image '{}' has an invalid resolution.";
    static constexpr AStringView s_MismatchFailureLabel = "tex_conv: all LDR texture inputs must have the same resolution.";
    [[nodiscard]] static bool decode(const AString& inputPathText, Plane& outPlane){
        return basisu::load_image(inputPathText.c_str(), outPlane);
    }
};
struct HdrPlaneLoader{
    using Plane = basisu::imagef;
    static constexpr AStringView s_DecodeFailureLabel = "tex_conv: failed to decode HDR image '{}'.";
    static constexpr AStringView s_ResolutionFailureLabel = "tex_conv: HDR image '{}' has an invalid resolution.";
    static constexpr AStringView s_MismatchFailureLabel = "tex_conv: all HDR texture inputs must have the same resolution.";
    [[nodiscard]] static bool decode(const AString& inputPathText, Plane& outPlane){
        return basisu::load_image_hdr(inputPathText.c_str(), outPlane, false);
    }
};
template<typename PlaneLoader, typename PlaneVector>
[[nodiscard]] bool LoadPlanesFromFiles(const Vector<Path>& inputPaths, PlaneVector& outPlanes){
    using Plane = typename PlaneLoader::Plane;
    outPlanes.clear();
    if(inputPaths.empty())
        return false;
    u32 width = 0u;
    u32 height = 0u;
    outPlanes.reserve(inputPaths.size());
    for(const Path& inputPath : inputPaths){
        const AString inputPathText = PathToGenericString<AString>(inputPath);
        Plane plane;
        if(!PlaneLoader::decode(inputPathText, plane)){
            NWB_LOGGER_ERROR(NWB_TEXT(PlaneLoader::s_DecodeFailureLabel), PathToString<tchar>(inputPath));
            return false;
        }
        if(plane.get_width() == 0u || plane.get_height() == 0u){
            NWB_LOGGER_ERROR(NWB_TEXT(PlaneLoader::s_ResolutionFailureLabel), PathToString<tchar>(inputPath));
            return false;
        }
        if(outPlanes.empty()){
            width = plane.get_width();
            height = plane.get_height();
        }
        else if(plane.get_width() != width || plane.get_height() != height){
            NWB_LOGGER_ERROR(NWB_TEXT(PlaneLoader::s_MismatchFailureLabel));
            return false;
        }
        outPlanes.push_back(Move(plane));
    }
    return true;
}
// Shared volume-mip prologue: derive the next-level dims from the source planes and size the output planes.
template<typename PlaneVector>
[[nodiscard]] bool PrepareVolumeMipTargets(
    const PlaneVector& sourcePlanes,
    const u32 sourceWidth,
    const u32 sourceHeight,
    PlaneVector& outPlanes,
    VolumeMipDims& outDims
){
    if(sourcePlanes.empty())
        return false;
    if(!ComputeVolumeMipDims(sourceWidth, sourceHeight, static_cast<u32>(sourcePlanes.size()), outDims))
        return false;
    outPlanes.clear();
    outPlanes.resize(outDims.targetDepth);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TEX_CONV_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

