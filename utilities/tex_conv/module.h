// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TEX_CONV_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_UastcBlockWidth = 4u;
inline constexpr u32 s_UastcBlockHeight = 4u;
inline constexpr u32 s_UastcBytesPerBlock = 16u;
inline constexpr AStringView s_UastcSpecificationRevision = "b624c07ad3c659e7b0f0badcb36e9a6b8820a99d";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MipLevel{
    u32 level = 0u;
    u32 width = 0u;
    u32 height = 0u;
    u32 blocksX = 0u;
    u32 blocksY = 0u;
    u64 offsetBytes = 0u;
    u64 sizeBytes = 0u;
};

struct TexturePayload{
    u32 width = 0u;
    u32 height = 0u;
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
bool EncodeTexture(const Path& inputPath, bool srgb, TexturePayload& outPayload);
bool WriteOutputs(const OutputPaths& outputPaths, const TexturePayload& payload, bool srgb, bool force);

int Run(int argc, char** argv);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TEX_CONV_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

