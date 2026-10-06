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


// Large graph-owned compute effects may use an auxiliary physical queue only when the device-wide same-class policy is enabled. Keep each direct successor on the initially chosen transport: the effect remains one semantic packet while the compiler owns its exact inter-packet waits.
// Cross-family routing stays separately disabled here until an effect-specific ownership/performance decision promotes it.
void EnableSameFamilyComputeEffectRouting(Core::GpuTaskSchedulingHint& scheduling, const bool preserveDirectDependency = true)noexcept;

// A cross-family route remains a second explicit opt-in. The compiler validates every declared resource against its concurrent-sharing contract and lowers paired ownership barriers for any exclusive crossing.
inline void EnableCrossFamilyComputeEffectRouting(Core::GpuTaskSchedulingHint& scheduling)noexcept{
    scheduling.allowCrossFamilySameClassQueueRouting = true;
}


// Shared alternating generate/raster compute-emulation chain scheduling: keep the full chain in one packet so a single list owns timing and handoff.
// Occupancy, extinction, and accumulation share this policy and differ only in their downstream consumers.
[[nodiscard]] Core::GpuTaskSchedulingHint SharedComputeEmulationChainScheduling()noexcept;

// Shared phase descriptors retain one ordered recording group while the compiler selects its compatible physical queue.
void MakeSharedComputeEmulationPhaseTaskDesc(
    Core::GpuTaskDesc& desc,
    const Name identity,
    const AStringView markerLabel,
    const Core::GpuTaskId& dependency,
    const Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena>& resourceUses,
    const Core::GpuTaskResourceSetUse* const resourceSetUses,
    const usize resourceSetUseCount
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

