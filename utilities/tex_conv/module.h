// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <impl/assets_texture/format.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TEX_CONV_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TextureDimension = Impl::TextureDimension;
namespace TexturePayloadFormat = Impl::TexturePayloadFormat;
namespace TextureAlphaMode = Impl::TextureAlphaMode;
using Impl::TextureFormat::ComputeCompleteMipCount;
using Impl::TextureFormat::ComputeMipPlaneBlockLayout;
using Impl::TextureFormat::ComputePlaneBlockLayout;
using Impl::TextureFormat::GetTexturePayloadBlockLayout;
using Impl::TextureFormat::s_ClampMipAddressMode;
using Impl::TextureFormat::s_AlphaConstantUnorm8Mode;
using Impl::TextureFormat::s_AlphaOpaqueMode;
using Impl::TextureFormat::s_AlphaUastcLdr4x4Mode;
using Impl::TextureFormat::s_LinearColorSpace;
using Impl::TextureFormat::s_MipMajorSliceMajorBlocksPayloadLayout;
using Impl::TextureFormat::s_SrgbColorSpace;
using Impl::TextureFormat::s_Texture2DDimension;
using Impl::TextureFormat::s_Texture3DDimension;
using Impl::TextureFormat::s_TextureCubeDimension;
using Impl::TextureFormat::s_TextureCubeFaceCount;
using Impl::TextureFormat::s_UastcBlockHeight;
using Impl::TextureFormat::s_UastcBlockWidth;
using Impl::TextureFormat::s_UastcBytesPerBlock;
using Impl::TextureFormat::s_UastcLdr4x4Format;
using Impl::TextureFormat::s_UastcLdrTextureMetadataVersion;
using Impl::TextureFormat::s_UastcHdr4x4Format;
using Impl::TextureFormat::s_UastcHdrTextureMetadataVersion;
using Impl::TextureFormat::s_UastcSpecificationRevision;

namespace AlphaSourceMode{
    enum Enum : u8{
        Original,
        Constant,
        Image
    };
};

struct AlphaSource{
    AlphaSourceMode::Enum mode = AlphaSourceMode::Original;
    f32 constant = 1.0f;
    Path path;

    AlphaSource()
        : path(UtilityDetail::Arena())
    {}
};

struct MipLevel{
    u64 offsetBytes = 0u;
    u64 sizeBytes = 0u;

    u32 level = 0u;
    u32 width = 0u;
    u32 height = 0u;
    u32 blocksX = 0u;
    u32 blocksY = 0u;
    u32 sliceCount = 1u;
};

struct TexturePayload{
    Vector<MipLevel> mips;
    Vector<u8> bytes;
    // Present only for HDR textures using SeparateUastcLdr4x4. It is written
    // immediately after bytes in the same .tex sidecar.
    Vector<u8> alphaBytes;

    u32 width = 0u;
    u32 height = 0u;
    u32 depth = 1u;

    TextureDimension::Enum dimension = TextureDimension::Texture2D;
    TexturePayloadFormat::Enum format = TexturePayloadFormat::UastcLdr4x4;
    TextureAlphaMode::Enum alphaMode = TextureAlphaMode::Opaque;
    u8 alphaConstantUnorm8 = 255u;
    bool srgb = false;
    bool hasAlpha = false;
};

struct OutputPaths{
    Path metadata;
    Path data;
    Path metadataTemporary;
    Path dataTemporary;

    OutputPaths()
        : metadata(UtilityDetail::Arena())
        , data(UtilityDetail::Arena())
        , metadataTemporary(UtilityDetail::Arena())
        , dataTemporary(UtilityDetail::Arena())
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool IsSupportedInputPath(const Path& path);
bool IsHdrInputPath(const Path& path);
bool ResolveOutputPaths(const Path& inputPath, const AString& outputArgument, OutputPaths& outOutputPaths);
bool ValidateOutputPaths(const OutputPaths& outputPaths, bool force);
bool EncodeTexture(
    const Vector<Path>& inputPaths,
    TextureDimension::Enum dimension,
    bool srgb,
    const AlphaSource& alphaSource,
    TexturePayload& outPayload
);
bool WriteOutputs(const OutputPaths& outputPaths, const TexturePayload& payload, bool force);

int Run(int argc, char** argv);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TEX_CONV_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

