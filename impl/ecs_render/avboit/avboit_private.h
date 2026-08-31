// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/avboit/avboit.h>
#include <impl/ecs_render/avboit/avboit_system.h>
#include <impl/ecs_render/csg/csg_system.h>
#include <impl/ecs_render/kernel/renderer_format_private.h>
#include <impl/ecs_render/kernel/timing_names.h>
#include <impl/ecs_render/material/material_system.h>
#include <impl/ecs_render/material/renderer_render_state_private.h>
#include <impl/ecs_render/shader/shader_system.h>
#include <impl/ecs_render/shared/renderer_push_constants_private.h>
#include <impl/ecs_render/avboit/renderer_avboit_state.h>

#include <core/common/log.h>
#include <core/graphics/module.h>
#include <core/graphics/shader_archive.h>

#include <impl/assets/graphics/avboit/binding_slots.h>
#include <impl/assets/graphics/avboit/constants.h>
#include <impl/assets/graphics/avboit/names.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderAvboitDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_AvboitDownsample = 4u;
inline constexpr u32 s_AvboitVirtualSlices = 128u;
inline constexpr u32 s_AvboitPhysicalSlices = 64u;
inline constexpr u32 s_AvboitExtinctionSlicesPerWord = NWB_AVBOIT_EXTINCTION_SLICES_PER_WORD;
inline constexpr f32 s_AvboitExtinctionFixedScale = 45.985905f;
inline constexpr f32 s_AvboitSelfOcclusionSliceBias = 2.f;
inline constexpr usize s_AvboitControlWordCount = NWB_AVBOIT_CONTROL_WORD_COUNT;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

