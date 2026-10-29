// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <core/task/gpu/task_desc.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Large graph-owned compute effects may use an auxiliary physical queue only when the device-wide same-class policy is enabled.
// Keep each successor on its first transport: one semantic packet while the compiler owns inter-packet waits.
// Cross-family routing stays separately disabled here until an effect-specific ownership/performance decision promotes it.
void EnableSameFamilyComputeEffectRouting(Core::GpuTaskSchedulingHint& scheduling, const b8 preserveDirectDependency = true)noexcept;

// Cross-family routes stay second opt-in. The compiler checks resources against sharing contracts and lowers paired barriers.
inline void EnableCrossFamilyComputeEffectRouting(Core::GpuTaskSchedulingHint& scheduling)noexcept{
    scheduling.allowCrossFamilySameClassQueueRouting = true;
}


// Shared alternating generate/raster compute-emulation chain scheduling: keep the full chain in one packet so a single list owns timing and handoff.
// Occupancy, extinction, and accumulation share this policy and differ only in their downstream consumers.
[[nodiscard]] Core::GpuTaskSchedulingHint SharedComputeEmulationChainScheduling()noexcept;

// Shared phase descriptors retain one ordered recording group while the compiler selects its compatible physical queue.
void MakeSharedComputeEmulationPhaseTaskDesc(
    const Name identity,
    const AStringView markerLabel,
    const Core::GpuTaskId& dependency,
    Span<const Core::GpuTaskResourceUse> resourceUses,
    Span<const Core::GpuTaskResourceSetUse> resourceSetUses,
    Core::GpuTaskDesc& desc
)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

