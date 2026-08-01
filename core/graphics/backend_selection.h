// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/global.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Legacy backend-selection compatibility


// The renderer is Vulkan-only.  Keep the old configuration macros and public constants available to source clients,
// but reject attempts to select the removed Metal path at preprocessing time instead of silently building Vulkan.
#if defined(NWB_GRAPHICS_BACKEND_METAL) && NWB_GRAPHICS_BACKEND_METAL
#error "Metal is no longer a supported NWB graphics backend; remove NWB_GRAPHICS_BACKEND_METAL."
#endif

#if defined(NWB_GRAPHICS_BACKEND_VULKAN) && !NWB_GRAPHICS_BACKEND_VULKAN
#error "NWB graphics is Vulkan-only; NWB_GRAPHICS_BACKEND_VULKAN must be enabled."
#endif

#if !defined(NWB_GRAPHICS_BACKEND_VULKAN)
#define NWB_GRAPHICS_BACKEND_VULKAN 1
#endif

#if !defined(NWB_GRAPHICS_BACKEND_METAL)
#define NWB_GRAPHICS_BACKEND_METAL 0
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GraphicsAPI{
    // METAL remains an enum value solely for source compatibility with legacy comparisons.  It is never selected.
    enum Enum : u8{
        VULKAN,
        METAL,
    };
};

namespace GraphicsBackend{
    inline constexpr GraphicsAPI::Enum s_Api = GraphicsAPI::VULKAN;
    inline constexpr bool s_ResizeSwapChainOnVSyncChange = true;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Preserve the legacy header's role as the concrete-backend include point while keeping the renderer Vulkan-only.
#include "vulkan/backend_context.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
