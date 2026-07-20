#pragma once


#include <impl/ecs_render/kernel/renderer_types.h>

#include <core/graphics/module.h>
#include <core/graphics/backend_selection.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<usize N>
inline Core::Format::Enum SelectSupportedFormat(
    Core::Device& device,
    const Core::Format::Enum (&candidates)[N],
    const Core::FormatSupport::Mask requiredSupport
){
    for(const Core::Format::Enum format : candidates){
        if((device.queryFormatSupport(format) & requiredSupport) == requiredSupport)
            return format;
    }

    return Core::Format::UNKNOWN;
}

inline bool CreateClampSampler(
    Core::Device& device,
    Core::SamplerHandle& sampler,
    const bool linearFiltering
){
    if(sampler)
        return true;

    Core::SamplerDesc samplerDesc;
    samplerDesc
        .setAllFilters(linearFiltering)
        .setAllAddressModes(Core::SamplerAddressMode::Clamp)
    ;
    sampler = device.createSampler(samplerDesc);
    if(sampler)
        return true;

    return false;
}

inline Core::Format::Enum SelectGBufferAlbedoFormat(Core::Device& device){
    constexpr Core::Format::Enum candidates[] = {
        Core::Format::RGBA16_FLOAT,
    };
    constexpr Core::FormatSupport::Mask requiredSupport = Core::FormatSupport::Texture | Core::FormatSupport::RenderTarget;

    return SelectSupportedFormat(device, candidates, requiredSupport);
}

inline Core::Format::Enum SelectGBufferVectorFormat(Core::Device& device){
    constexpr Core::Format::Enum candidates[] = {
        Core::Format::RGBA16_FLOAT,
    };
    constexpr Core::FormatSupport::Mask requiredSupport = Core::FormatSupport::Texture | Core::FormatSupport::RenderTarget;

    return SelectSupportedFormat(device, candidates, requiredSupport);
}

inline Core::Format::Enum SelectGBufferDepthFormat(Core::Device& device){
    constexpr Core::Format::Enum candidates[] = {
        Core::Format::D32,
        Core::Format::D24S8,
        Core::Format::D16,
    };
    constexpr Core::FormatSupport::Mask requiredSupport = Core::FormatSupport::DepthStencil;

    return SelectSupportedFormat(device, candidates, requiredSupport);
}

inline Core::Format::Enum SelectCsgCapNormalFormat(Core::Device& device){
    constexpr Core::Format::Enum candidates[] = {
        NWB_CSG_CAP_NORMAL_CORE_FORMAT,
    };
    constexpr Core::FormatSupport::Mask requiredSupport =
        Core::FormatSupport::Texture
        | Core::FormatSupport::RenderTarget
        | Core::FormatSupport::ShaderUavStore
    ;

    return SelectSupportedFormat(device, candidates, requiredSupport);
}

inline Core::Format::Enum SelectCsgIntervalDepthFormat(Core::Device& device){
    constexpr Core::Format::Enum candidates[] = {
        NWB_CSG_INTERVAL_DEPTH_CORE_FORMAT,
    };
    constexpr Core::FormatSupport::Mask requiredSupport =
        Core::FormatSupport::Texture
        | Core::FormatSupport::RenderTarget
        | Core::FormatSupport::ShaderUavStore
    ;

    return SelectSupportedFormat(device, candidates, requiredSupport);
}

inline Core::Format::Enum SelectCsgIntervalIdFormat(Core::Device& device){
    constexpr Core::Format::Enum candidates[] = {
        NWB_CSG_INTERVAL_ID_CORE_FORMAT,
    };
    constexpr Core::FormatSupport::Mask requiredSupport =
        Core::FormatSupport::Texture
        | Core::FormatSupport::RenderTarget
        | Core::FormatSupport::ShaderUavStore
    ;

    return SelectSupportedFormat(device, candidates, requiredSupport);
}

inline Core::Format::Enum SelectCsgReceiverEventDataFormat(Core::Device& device){
    constexpr Core::Format::Enum candidates[] = {
        NWB_CSG_RECEIVER_EVENT_DATA_CORE_FORMAT,
    };
    constexpr Core::FormatSupport::Mask requiredSupport =
        Core::FormatSupport::Texture
        | Core::FormatSupport::ShaderUavStore
    ;

    return SelectSupportedFormat(device, candidates, requiredSupport);
}

inline Core::Format::Enum SelectCsgReceiverEventCountFormat(Core::Device& device){
    constexpr Core::Format::Enum candidates[] = {
        NWB_CSG_RECEIVER_EVENT_COUNT_CORE_FORMAT,
    };
    constexpr Core::FormatSupport::Mask requiredSupport =
        Core::FormatSupport::Texture
        | Core::FormatSupport::ShaderUavStore
    ;

    return SelectSupportedFormat(device, candidates, requiredSupport);
}

inline Core::Format::Enum SelectCsgReceiverSpanDataFormat(Core::Device& device){
    constexpr Core::Format::Enum candidates[] = {
        NWB_CSG_RECEIVER_SPAN_DATA_CORE_FORMAT,
    };
    constexpr Core::FormatSupport::Mask requiredSupport =
        Core::FormatSupport::Texture
        | Core::FormatSupport::ShaderUavStore
    ;

    return SelectSupportedFormat(device, candidates, requiredSupport);
}

inline Core::Format::Enum SelectCsgReceiverSpanCountFormat(Core::Device& device){
    constexpr Core::Format::Enum candidates[] = {
        NWB_CSG_RECEIVER_SPAN_COUNT_CORE_FORMAT,
    };
    constexpr Core::FormatSupport::Mask requiredSupport =
        Core::FormatSupport::Texture
        | Core::FormatSupport::ShaderUavStore
    ;

    return SelectSupportedFormat(device, candidates, requiredSupport);
}

inline Core::Format::Enum SelectCsgRemovedIntervalDepthFormat(Core::Device& device){
    constexpr Core::Format::Enum candidates[] = {
        NWB_CSG_REMOVED_INTERVAL_DEPTH_CORE_FORMAT,
    };
    constexpr Core::FormatSupport::Mask requiredSupport =
        Core::FormatSupport::Texture
        | Core::FormatSupport::ShaderUavStore
    ;

    return SelectSupportedFormat(device, candidates, requiredSupport);
}

inline Core::Format::Enum SelectCsgRemovedIntervalCapNormalFormat(Core::Device& device){
    constexpr Core::Format::Enum candidates[] = {
        NWB_CSG_REMOVED_INTERVAL_CAP_NORMAL_CORE_FORMAT,
    };
    constexpr Core::FormatSupport::Mask requiredSupport =
        Core::FormatSupport::Texture
        | Core::FormatSupport::ShaderUavStore
    ;

    return SelectSupportedFormat(device, candidates, requiredSupport);
}

inline Core::Format::Enum SelectCsgRemovedIntervalDataFormat(Core::Device& device){
    constexpr Core::Format::Enum candidates[] = {
        NWB_CSG_REMOVED_INTERVAL_DATA_CORE_FORMAT,
    };
    constexpr Core::FormatSupport::Mask requiredSupport =
        Core::FormatSupport::Texture
        | Core::FormatSupport::ShaderUavStore
    ;

    return SelectSupportedFormat(device, candidates, requiredSupport);
}

inline Core::Format::Enum SelectCsgRemovedIntervalCountFormat(Core::Device& device){
    constexpr Core::Format::Enum candidates[] = {
        NWB_CSG_REMOVED_INTERVAL_COUNT_CORE_FORMAT,
    };
    constexpr Core::FormatSupport::Mask requiredSupport =
        Core::FormatSupport::Texture
        | Core::FormatSupport::ShaderUavStore
    ;

    return SelectSupportedFormat(device, candidates, requiredSupport);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

