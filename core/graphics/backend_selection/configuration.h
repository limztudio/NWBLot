// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_BACKEND_METAL
#define NWB_GRAPHICS_BACKEND_METAL 0
#endif

#ifndef NWB_GRAPHICS_BACKEND_VULKAN
#define NWB_GRAPHICS_BACKEND_VULKAN (!NWB_GRAPHICS_BACKEND_METAL)
#endif

#if (NWB_GRAPHICS_BACKEND_VULKAN != 0 && NWB_GRAPHICS_BACKEND_VULKAN != 1) || (NWB_GRAPHICS_BACKEND_METAL != 0 && NWB_GRAPHICS_BACKEND_METAL != 1)
#error Graphics backend selection flags must be 0 or 1.
#endif

#if (NWB_GRAPHICS_BACKEND_VULKAN + NWB_GRAPHICS_BACKEND_METAL) != 1
#error Select exactly one graphics backend.
#endif

#if NWB_GRAPHICS_BACKEND_METAL
#error The Metal graphics backend is not implemented yet.
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

