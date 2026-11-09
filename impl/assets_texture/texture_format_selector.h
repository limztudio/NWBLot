// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "asset.h"

#include <core/graphics/api.h>
#include <core/graphics/backend_selection.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Core::FormatSupport::Mask s_RequiredTextureFormatSupport = Core::FormatSupport::Texture | Core::FormatSupport::ShaderSample;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class TextureFormatSelector final{
private:
    [[nodiscard]] static b8 SupportsTextureFormat(const Core::Format::Enum format, Core::Device& device)noexcept{
        return (device.queryFormatSupport(format) & s_RequiredTextureFormatSupport) == s_RequiredTextureFormatSupport;
    }


public:
    [[nodiscard]] static Core::Format::Enum SelectUploadFormat(const Texture& textureAsset, Core::Device& device)noexcept{
        if(textureAsset.payloadFormat() == TexturePayloadFormat::UastcHdr4x4){
            if(textureAsset.alphaMode() == TextureAlphaMode::Opaque){
                if(SupportsTextureFormat(Core::Format::ASTC_4x4_FLOAT, device))
                    return Core::Format::ASTC_4x4_FLOAT;
                if(SupportsTextureFormat(Core::Format::BC6H_UFLOAT, device))
                    return Core::Format::BC6H_UFLOAT;
            }
            return SupportsTextureFormat(Core::Format::RGBA16_FLOAT, device) ? Core::Format::RGBA16_FLOAT : Core::Format::UNKNOWN;
        }
        if(textureAsset.payloadFormat() != TexturePayloadFormat::UastcLdr4x4)
            return Core::Format::UNKNOWN;

        if(SupportsTextureFormat(Core::Format::ASTC_4x4_UNORM, device))
            return Core::Format::ASTC_4x4_UNORM;

        if(SupportsTextureFormat(Core::Format::BC7_UNORM, device))
            return Core::Format::BC7_UNORM;

        return SupportsTextureFormat(Core::Format::RGBA8_UNORM, device) ? Core::Format::RGBA8_UNORM : Core::Format::UNKNOWN;
    }

    [[nodiscard]] static Core::Format::Enum SelectHdrOpaqueUploadFallback(const Core::Format::Enum failedFormat, Core::Device& device)noexcept{
        if(failedFormat == Core::Format::ASTC_4x4_FLOAT && SupportsTextureFormat(Core::Format::BC6H_UFLOAT, device))
            return Core::Format::BC6H_UFLOAT;
        if(failedFormat != Core::Format::RGBA16_FLOAT && SupportsTextureFormat(Core::Format::RGBA16_FLOAT, device))
            return Core::Format::RGBA16_FLOAT;
        return Core::Format::UNKNOWN;
    }

    [[nodiscard]] static Core::Format::Enum SelectLdrUploadFallback(const Core::Format::Enum failedFormat, Core::Device& device)noexcept{
        if(failedFormat == Core::Format::ASTC_4x4_UNORM && SupportsTextureFormat(Core::Format::BC7_UNORM, device))
            return Core::Format::BC7_UNORM;
        if(failedFormat != Core::Format::RGBA8_UNORM && SupportsTextureFormat(Core::Format::RGBA8_UNORM, device))
            return Core::Format::RGBA8_UNORM;
        return Core::Format::UNKNOWN;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
