// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"

#include <core/graphics/vulkan/arena_names.adhd.h>
#include <core/graphics/vulkan/context_labels.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanArenaScope{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr TStringView s_TaskStageLabel = NWB_TEXT("task");
inline constexpr TStringView s_MeshStageLabel = NWB_TEXT("mesh");
inline constexpr TStringView s_FragmentStageLabel = NWB_TEXT("fragment");
inline constexpr TStringView s_BufferResourceLabel = NWB_TEXT("buffer");
inline constexpr TStringView s_TextureResourceLabel = NWB_TEXT("texture");
inline constexpr TStringView s_WriteBufferCommandLabel = NWB_TEXT("write buffer");
inline constexpr TStringView s_SetComputeStateCommandLabel = NWB_TEXT("set compute state");
inline constexpr TStringView s_SetPermanentTextureStateCommandLabel = NWB_TEXT("set permanent texture state");
inline constexpr TStringView s_DispatchMeshCommandLabel = NWB_TEXT("dispatch mesh");
inline constexpr TStringView s_DispatchIndirectCommandLabel = NWB_TEXT("dispatch indirect");
using VulkanDetail::s_PresentationSignalCancellationContext;
using VulkanDetail::s_AcquireSlotReuseContext;
using VulkanDetail::s_AcquireNextImageContext;
using VulkanDetail::s_QueueSubmitContext;
using VulkanDetail::s_NativePresentAdmissionContext;
using VulkanDetail::s_AcquiredImageSemaphoreBridgeContext;
using VulkanDetail::s_AbandonedPresentationSignalIdleContext;
using VulkanDetail::s_UnconsumedPresentationSignalIdleContext;
using VulkanDetail::s_PresentContext;
using VulkanDetail::s_QueueTimelineQueryContext;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

