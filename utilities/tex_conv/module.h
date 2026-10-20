// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <global/span.h>
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
inline constexpr AStringView s_BackupOutputSuffix = ".old";

struct AlphaSource{
    AlphaSourceMode::Enum mode = AlphaSourceMode::Original;
    f32 constant = 1.0f;
    ScratchPath path;


public:
    explicit AlphaSource(Core::Alloc::ScratchArena& scratchArena)noexcept
        : path(scratchArena)
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


public:
    OutputPaths()
        : metadata(UtilityDetail::Arena())
        , data(UtilityDetail::Arena())
        , metadataTemporary(UtilityDetail::Arena())
        , dataTemporary(UtilityDetail::Arena())
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool IsSupportedInputPath(const ScratchPath& path);
[[nodiscard]] bool IsHdrInputPath(const ScratchPath& path);
[[nodiscard]] Expected<OutputPaths> ResolveOutputPaths(const ScratchPath& inputPath, AStringView outputArgument, Core::Alloc::ScratchArena& scratchArena);
[[nodiscard]] bool ValidateOutputPaths(const OutputPaths& outputPaths, bool force, Core::Alloc::ScratchArena& scratchArena);
[[nodiscard]] Expected<PreparedTexture> PrepareTexture(Span<const ScratchPath> inputPaths, TextureDimension::Enum dimension, bool srgb, const AlphaSource& alphaSource, Core::Alloc::ScratchArena& scratchArena);
[[nodiscard]] bool WriteOutputs(const OutputPaths& outputPaths, const PreparedTexture& payload, bool force, Core::Alloc::ScratchArena& scratchArena);

int Run(int argc, char** argv);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TEX_CONV_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

