// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "scene_resources.h"

#include <impl/ecs_render/shared/frame_types.adhd.h>
#include <impl/ecs_render/raytrace/types.adhd.h>
#include <impl/ecs_render/shadow/quality_settings.adhd.h>
#include <impl/assets/graphics/shadow/sw_binding_slots.h>

#include <core/graphics/rhi/command.h>

#include <global/simdmath.h>
#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{
    using NWB::Impl::SceneLightGpuData;
};

// Only accepted static scene/light identities can authorize a reduced per-pixel sampling budget.
class SoftwareTransparentSamplingHistory final{
public:
    void prepareScene(const RayTracingSceneContentStamp& scene)noexcept;
    void prepareLighting(Span<const ECSRenderDetail::SceneLightGpuData> lights)noexcept;
    void accept()noexcept;
    void discard()noexcept{ m_accepted = false; }
    [[nodiscard]] b8 usable()const noexcept;


private:
    RayTracingSceneContentStamp m_scene;
    RayTracingSceneContentStamp m_acceptedScene;
    u64 m_lightHash = 0u;
    u64 m_acceptedLightHash = 0u;
    b8 m_lightingPrepared = false;
    b8 m_accepted = false;
};

struct SoftwareTransparentSamplingState{
    Core::BindingLayoutHandle m_layout;
    // Index zero keeps the fixed budget; index one consumes accepted sampling history.
    Core::ShaderHandle m_shaders[2];
    Core::ComputePipelineHandle m_pipelines[2];
    SoftwareTransparentSamplingHistory m_history;
};

static_assert(sizeof(SoftwareTransparentSamplingPush) == 116u);
static_assert(offsetof(SoftwareTransparentSamplingPush, receiverFactor) == 112u);
static_assert(offsetof(SoftwareTransparentSamplingPush, prevWorldToClip) == s_SoftwareTransparentSamplingPushPrevWorldToClipOffset);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

