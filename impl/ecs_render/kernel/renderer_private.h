
#pragma once


#include <impl/ecs_render/kernel/system.h>

#include <impl/ecs_render/avboit/avboit.h>
#include <impl/ecs_render/material/material_typed_private.h>
#include <impl/ecs_render/mesh/mesh_view_private.h>
#include <impl/ecs_render/kernel/renderer_constants_private.h>
#include <impl/ecs_render/kernel/renderer_format_private.h>
#include <impl/ecs_render/shared/renderer_push_constants_private.h>
#include <impl/ecs_render/material/renderer_render_state_private.h>
#include <impl/ecs_render/deferred/renderer_scene_private.h>
#include <impl/ecs_render/kernel/timing_names.h>

#include <global/core/assets/manager.h>
#include <global/core/common/log.h>
#include <global/core/ecs/world.h>
#include <global/core/graphics/module.h>
#include <global/core/graphics/shader_archive.h>
#include <impl/assets_mesh/asset.h>
#include <impl/assets/graphics/avboit/binding_slots.h>
#include <impl/assets/graphics/avboit/constants.h>
#include <impl/assets/graphics/csg/binding_slots.h>
#include <impl/assets/graphics/csg/constants.h>
#include <impl/assets/graphics/deferred/binding_slots.h>
#include <impl/assets/graphics/mesh/runtime_constants.h>
#include <impl/assets/graphics/scene/binding_slots.h>
#include <impl/assets_material/asset.h>
#include <impl/assets_material/shader_stage_names.h>
#include <impl/assets_shader/loader.h>
#include <impl/ecs_csg/module.h>
#include <impl/ecs_scene/module.h>
#include <impl/ecs_mesh/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

