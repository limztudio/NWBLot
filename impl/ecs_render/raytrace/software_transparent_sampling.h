// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "scene_resources.h"

#include <core/graphics/rhi/command.h>
#include <global/simdmath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{
    struct SceneLightGpuData;
};

// Only accepted static scene/light identities can authorize a reduced per-pixel sampling budget.
class SoftwareTransparentSamplingHistory final{
public:
    void prepareScene(const RayTracingSceneContentStamp& scene)noexcept;
    void prepareLighting(const ECSRenderDetail::SceneLightGpuData* lights, u32 lightCount)noexcept;
    void accept()noexcept;
    void discard()noexcept;
    [[nodiscard]] bool usable()const noexcept;

private:
    RayTracingSceneContentStamp m_scene;
    RayTracingSceneContentStamp m_acceptedScene;
    u64 m_lightHash = 0u;
    u64 m_acceptedLightHash = 0u;
    bool m_lightingPrepared = false;
    bool m_accepted = false;
};

struct SoftwareTransparentSamplingState{
    Core::BindingLayoutHandle m_layout;
    // Index zero keeps the fixed budget; index one consumes accepted sampling history.
    Core::ShaderHandle m_shaders[2];
    Core::ComputePipelineHandle m_pipelines[2];
    SoftwareTransparentSamplingHistory m_history;
};

// Dedicated transparent trace ABI leaves all opaque, hardware and coarse software kernels unchanged.
struct SoftwareTransparentSamplingPush{
    u32 width = 0u;
    u32 height = 0u;
    u32 instanceCount = 0u;
    u32 frameIndex = 0u;
    u32 deferredResourcesHeapSlot = 0u;
    u32 materialContextSlotsHeapSlot = 0u;
    u32 transparentSoftHalfStorageSlot = 0u;
    u32 softSampleCount = 3u;
    u32 historyValid = 0u;
    u32 geometryCurrSlot = 0u;
    u32 geometryPrevSlot = 0u;
    u32 momentsInSlot = 0u;
    Float44U prevWorldToClip = {};
};
static_assert(sizeof(SoftwareTransparentSamplingPush) == 112u);
static constexpr usize s_SoftwareTransparentSamplingPushPrevWorldToClipOffset = 48u;
static_assert(offsetof(SoftwareTransparentSamplingPush, prevWorldToClip) == s_SoftwareTransparentSamplingPushPrevWorldToClipOffset);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

