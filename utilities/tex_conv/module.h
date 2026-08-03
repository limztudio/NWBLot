// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <impl/assets_texture/format.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TEX_CONV_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TextureDimension = Impl::TextureDimension;
using Impl::TextureFormat::ComputeCompleteMipCount;
using Impl::TextureFormat::ComputePlaneBlockLayout;
using Impl::TextureFormat::s_ClampMipAddressMode;
using Impl::TextureFormat::s_LinearColorSpace;
using Impl::TextureFormat::s_MipMajorSliceMajorBlocksPayloadLayout;
using Impl::TextureFormat::s_SrgbColorSpace;
using Impl::TextureFormat::s_Texture2DDimension;
using Impl::TextureFormat::s_Texture3DDimension;
using Impl::TextureFormat::s_TextureCubeDimension;
using Impl::TextureFormat::s_TextureCubeFaceCount;
using Impl::TextureFormat::s_TextureMetadataVersion;
using Impl::TextureFormat::s_UastcBlockHeight;
using Impl::TextureFormat::s_UastcBlockWidth;
using Impl::TextureFormat::s_UastcBytesPerBlock;
using Impl::TextureFormat::s_UastcLdr4x4Format;
using Impl::TextureFormat::s_UastcSpecificationRevision;

struct MipLevel{
    u32 level = 0u;
    u32 width = 0u;
    u32 height = 0u;
    u32 blocksX = 0u;
    u32 blocksY = 0u;
    u64 offsetBytes = 0u;
    u64 sizeBytes = 0u;
    u32 sliceCount = 1u;
};

struct TexturePayload{
    TextureDimension::Enum dimension = TextureDimension::Texture2D;
    u32 width = 0u;
    u32 height = 0u;
    u32 depth = 1u;
    bool hasAlpha = false;
    Vector<MipLevel> mips;
    Vector<u8> bytes;
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


Path PathFromUtf8(const AString& value);
AString PathToUtf8(const Path& path);
bool IsSupportedInputPath(const Path& path);
bool ResolveOutputPaths(const Path& inputPath, const AString& outputArgument, OutputPaths& outOutputPaths);
bool ValidateOutputPaths(const OutputPaths& outputPaths, bool force);
bool EncodeTexture(const Vector<Path>& inputPaths, TextureDimension::Enum dimension, bool srgb, TexturePayload& outPayload);
bool WriteOutputs(const OutputPaths& outputPaths, const TexturePayload& payload, bool srgb, bool force);

int Run(int argc, char** argv);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TEX_CONV_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
