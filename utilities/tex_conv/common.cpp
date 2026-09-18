// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TEX_CONV_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool HasInputExtension(const Path& path, const AStringView* extensions, const usize extensionCount){
    const AString extension = LowerPathExtension<AString>(path);
    for(usize i = 0u; i < extensionCount; ++i){
        if(extension == extensions[i])
            return true;
    }
    return false;
}

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

template<typename ExtensionArray>
static bool HasListedInputExtension(const Path& path, const ExtensionArray& extensions){
    return HasInputExtension(path, extensions, LengthOf(extensions));
}

bool IsSupportedInputPath(const Path& path){
    return HasListedInputExtension(path, s_SupportedInputExtensions);
}

bool IsHdrInputPath(const Path& path){
    return HasListedInputExtension(path, s_HdrInputExtensions);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TEX_CONV_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

