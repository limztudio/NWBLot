// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/assets/graphics/caustic/photon_push_constants.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct DeferredFrameTargets;
struct CausticPhotonBudget;
class RendererRayTracingState;

namespace ECSRenderDetail{
    struct MeshViewBufferSnapshot;
};

// Shared CPU mirror of SW/HW caustic photon push constants.
struct CausticPhotonPushConstants{
#define NWB_CAUSTIC_PHOTON_PUSH_CONSTANT_FIELD(name, defaultValue) u32 name = defaultValue;
    NWB_CAUSTIC_PHOTON_PUSH_CONSTANTS_FIELDS(NWB_CAUSTIC_PHOTON_PUSH_CONSTANT_FIELD)
#undef NWB_CAUSTIC_PHOTON_PUSH_CONSTANT_FIELD
};
static_assert(sizeof(CausticPhotonPushConstants) == sizeof(u32) * 15u, "CausticPhotonPushConstants must match the shader push-constant layout");

[[nodiscard]] CausticPhotonPushConstants BuildCausticPhotonPushConstants(
    const DeferredFrameTargets& targets,
    const ECSRenderDetail::MeshViewBufferSnapshot& meshView,
    const RendererRayTracingState& rayTracingState,
    const CausticPhotonBudget& photonBudget,
    u32 instanceCount,
    u32 frameIndex,
    u32 temporalPhaseCount
)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

