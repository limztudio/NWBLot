// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TEX_CONV_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_PngExtension = ".png";
static constexpr AStringView s_JpgExtension = ".jpg";
static constexpr AStringView s_JpegExtension = ".jpeg";
static constexpr AStringView s_JfifExtension = ".jfif";
static constexpr AStringView s_TgaExtension = ".tga";
static constexpr AStringView s_QoiExtension = ".qoi";
static constexpr AStringView s_ExrExtension = ".exr";
static constexpr AStringView s_HdrExtension = ".hdr";

static constexpr AStringView s_SupportedInputExtensions[] = {
    s_PngExtension,
    s_JpgExtension,
    s_JpegExtension,
    s_JfifExtension,
    s_TgaExtension,
    s_QoiExtension,
    s_ExrExtension,
    s_HdrExtension,
};

static constexpr AStringView s_HdrInputExtensions[] = {
    s_ExrExtension,
    s_HdrExtension,
};

bool IsSupportedInputPath(const Path& path){
    return PathHasListedExtension(path, s_SupportedInputExtensions);
}

bool IsHdrInputPath(const Path& path){
    return PathHasListedExtension(path, s_HdrInputExtensions);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TEX_CONV_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

