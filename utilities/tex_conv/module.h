// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <global/texture_payload.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TEX_CONV_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AlphaSourceMode{
    enum Enum : u8{
        Original,
        Constant,
        Image
    };
};

inline constexpr AStringView s_AlphaWhiteKeyword = "white";
inline constexpr AStringView s_AlphaBlackKeyword = "black";
inline constexpr f32 s_AlphaWhiteConstant = 1.0f;
inline constexpr f32 s_AlphaBlackConstant = 0.0f;
inline constexpr AStringView s_TemporaryOutputSuffix = ".tmp";

struct AlphaSource{
    AlphaSourceMode::Enum mode = AlphaSourceMode::Original;
    f32 constant = 1.0f;
    Path path;

    AlphaSource()
        : path(UtilityDetail::Arena())
    {}
};

struct PreparedTexture{
    Vector<u8> bytes;

    u32 width = 0u;
    u32 height = 0u;
    u32 depth = 1u;
    u32 mipCount = 0u;

    TextureDimension::Enum dimension = TextureDimension::Texture2D;
    TexturePayloadFormat::Enum format = TexturePayloadFormat::UastcLdr4x4;
    TextureAlphaMode::Enum alphaMode = TextureAlphaMode::Opaque;
    u8 alphaConstantUnorm8 = TextureFormat::s_OpaqueAlphaUnorm8;
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
Expected<OutputPaths> ResolveOutputPaths(const Path& inputPath, const AStringView outputArgument);
bool ValidateOutputPaths(const OutputPaths& outputPaths, bool force);
Expected<PreparedTexture> PrepareTexture(Core::Alloc::ScratchArena& scratchArena, const Span<const Path> inputPaths, TextureDimension::Enum dimension, bool srgb, const AlphaSource& alphaSource);
bool WriteOutputs(const OutputPaths& outputPaths, const PreparedTexture& payload, bool force);

int Run(int argc, char** argv);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TEX_CONV_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

