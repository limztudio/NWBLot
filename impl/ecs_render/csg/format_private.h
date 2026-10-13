// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/kernel/renderer_format_private.h>
#include <impl/assets/graphics/csg/constants.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline Core::Format::Enum SelectCsgCapNormalFormat(Core::Device& device){
    constexpr Core::Format::Enum candidates[] = { NWB_CSG_CAP_NORMAL_CORE_FORMAT, };
    constexpr Core::FormatSupport::Mask requiredSupport = Core::FormatSupport::Texture | Core::FormatSupport::RenderTarget | Core::FormatSupport::ShaderUavStore;

    return SelectSupportedFormat(device, candidates, requiredSupport);
}

inline Core::Format::Enum SelectCsgIntervalDepthFormat(Core::Device& device){
    constexpr Core::Format::Enum candidates[] = { NWB_CSG_INTERVAL_DEPTH_CORE_FORMAT, };
    constexpr Core::FormatSupport::Mask requiredSupport = Core::FormatSupport::Texture | Core::FormatSupport::RenderTarget | Core::FormatSupport::ShaderUavStore;

    return SelectSupportedFormat(device, candidates, requiredSupport);
}

inline Core::Format::Enum SelectCsgIntervalIdFormat(Core::Device& device){
    constexpr Core::Format::Enum candidates[] = { NWB_CSG_INTERVAL_ID_CORE_FORMAT, };
    constexpr Core::FormatSupport::Mask requiredSupport = Core::FormatSupport::Texture | Core::FormatSupport::RenderTarget | Core::FormatSupport::ShaderUavStore;

    return SelectSupportedFormat(device, candidates, requiredSupport);
}

inline Core::Format::Enum SelectCsgReceiverEventDataFormat(Core::Device& device){
    constexpr Core::Format::Enum candidates[] = { NWB_CSG_RECEIVER_EVENT_DATA_CORE_FORMAT, };
    constexpr Core::FormatSupport::Mask requiredSupport = Core::FormatSupport::Texture | Core::FormatSupport::ShaderUavStore;

    return SelectSupportedFormat(device, candidates, requiredSupport);
}

inline Core::Format::Enum SelectCsgReceiverEventCountFormat(Core::Device& device){
    constexpr Core::Format::Enum candidates[] = { NWB_CSG_RECEIVER_EVENT_COUNT_CORE_FORMAT, };
    constexpr Core::FormatSupport::Mask requiredSupport = Core::FormatSupport::Texture | Core::FormatSupport::ShaderUavStore;

    return SelectSupportedFormat(device, candidates, requiredSupport);
}

inline Core::Format::Enum SelectCsgReceiverSpanDataFormat(Core::Device& device){
    constexpr Core::Format::Enum candidates[] = { NWB_CSG_RECEIVER_SPAN_DATA_CORE_FORMAT, };
    constexpr Core::FormatSupport::Mask requiredSupport = Core::FormatSupport::Texture | Core::FormatSupport::ShaderUavStore;

    return SelectSupportedFormat(device, candidates, requiredSupport);
}

inline Core::Format::Enum SelectCsgReceiverSpanCountFormat(Core::Device& device){
    constexpr Core::Format::Enum candidates[] = { NWB_CSG_RECEIVER_SPAN_COUNT_CORE_FORMAT, };
    constexpr Core::FormatSupport::Mask requiredSupport = Core::FormatSupport::Texture | Core::FormatSupport::ShaderUavStore;

    return SelectSupportedFormat(device, candidates, requiredSupport);
}

inline Core::Format::Enum SelectCsgRemovedIntervalDepthFormat(Core::Device& device){
    constexpr Core::Format::Enum candidates[] = { NWB_CSG_REMOVED_INTERVAL_DEPTH_CORE_FORMAT, };
    constexpr Core::FormatSupport::Mask requiredSupport = Core::FormatSupport::Texture | Core::FormatSupport::ShaderUavStore;

    return SelectSupportedFormat(device, candidates, requiredSupport);
}

inline Core::Format::Enum SelectCsgRemovedIntervalCapNormalFormat(Core::Device& device){
    constexpr Core::Format::Enum candidates[] = { NWB_CSG_REMOVED_INTERVAL_CAP_NORMAL_CORE_FORMAT, };
    constexpr Core::FormatSupport::Mask requiredSupport = Core::FormatSupport::Texture | Core::FormatSupport::ShaderUavStore;

    return SelectSupportedFormat(device, candidates, requiredSupport);
}

inline Core::Format::Enum SelectCsgRemovedIntervalDataFormat(Core::Device& device){
    constexpr Core::Format::Enum candidates[] = { NWB_CSG_REMOVED_INTERVAL_DATA_CORE_FORMAT, };
    constexpr Core::FormatSupport::Mask requiredSupport = Core::FormatSupport::Texture | Core::FormatSupport::ShaderUavStore;

    return SelectSupportedFormat(device, candidates, requiredSupport);
}

inline Core::Format::Enum SelectCsgRemovedIntervalCountFormat(Core::Device& device){
    constexpr Core::Format::Enum candidates[] = { NWB_CSG_REMOVED_INTERVAL_COUNT_CORE_FORMAT, };
    constexpr Core::FormatSupport::Mask requiredSupport = Core::FormatSupport::Texture | Core::FormatSupport::ShaderUavStore;

    return SelectSupportedFormat(device, candidates, requiredSupport);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

