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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TEX_CONV_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

