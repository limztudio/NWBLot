// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/kernel/renderer_format_private.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline Core::Format::Enum SelectGBufferAlbedoFormat(Core::Device& device){
    constexpr Core::Format::Enum candidates[] = { Core::Format::RGBA16_FLOAT, };
    constexpr Core::FormatSupport::Mask requiredSupport = Core::FormatSupport::Texture | Core::FormatSupport::RenderTarget;

    return SelectSupportedFormat(device, candidates, requiredSupport);
}

inline Core::Format::Enum SelectDeferredOpaqueColorFormat(Core::Device& device){
    constexpr Core::Format::Enum candidates[] = { Core::Format::RGBA16_FLOAT, };
    constexpr Core::FormatSupport::Mask requiredSupport = Core::FormatSupport::Texture | Core::FormatSupport::ShaderUavStore;

    return SelectSupportedFormat(device, candidates, requiredSupport);
}

inline Core::Format::Enum SelectGBufferVectorFormat(Core::Device& device){
    constexpr Core::Format::Enum candidates[] = { Core::Format::RGBA16_FLOAT, };
    constexpr Core::FormatSupport::Mask requiredSupport = Core::FormatSupport::Texture | Core::FormatSupport::RenderTarget;

    return SelectSupportedFormat(device, candidates, requiredSupport);
}

inline Core::Format::Enum SelectGBufferDepthFormat(Core::Device& device){
    constexpr Core::Format::Enum candidates[] = { Core::Format::D32, Core::Format::D24S8, Core::Format::D16, };
    constexpr Core::FormatSupport::Mask requiredSupport = Core::FormatSupport::DepthStencil;

    return SelectSupportedFormat(device, candidates, requiredSupport);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

