// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/kernel/renderer_private.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Core::GpuQueueRequest GraphicsQueueRequest(){
    return Core::GpuQueueRequest{
        Core::GpuQueueCapability::Graphics,
        Core::GpuQueuePreference::Graphics,
        false,
        false,
    };
}

// Built-in uploads require Transfer capability, while these small frame updates must stay on the Graphics packet
// that consumes them. Vulkan's Graphics transport advertises Transfer capability, so the compiler retains that
// physical route without introducing an asynchronous ownership handoff.
[[nodiscard]] inline Core::GpuQueueRequest GraphicsUploadQueueRequest(){
    return Core::GpuQueueRequest{
        Core::GpuQueueCapability::Transfer,
        Core::GpuQueuePreference::Graphics,
        false,
        false,
    };
}

[[nodiscard]] inline Core::GpuQueueRequest ComputeQueueRequest(){
    return Core::GpuQueueRequest{
        Core::GpuQueueCapability::Compute,
        Core::GpuQueuePreference::Compute,
        true,
        true,
    };
}

// Large graph-owned compute effects may use an auxiliary physical queue only when the device-wide same-class
// policy is enabled. Keep each direct successor on the initially chosen transport: the effect remains one
// semantic packet while the compiler owns its exact inter-packet waits. Cross-family routing stays separately
// disabled here until an effect-specific ownership/performance decision promotes it.
inline void EnableSameFamilyComputeEffectRouting(
    Core::GpuTaskSchedulingHint& scheduling,
    const bool preserveDirectDependency = true
){
    scheduling.allowSameClassQueueRouting = true;
    scheduling.preferNonPrimarySameClassQueue = true;
    scheduling.preserveSameClassQueueWithDirectDependency = preserveDirectDependency;
}

// A cross-family route remains a second explicit opt-in. The compiler validates every declared resource against
// its concurrent-sharing contract and lowers paired ownership barriers for any exclusive crossing.
inline void EnableCrossFamilyComputeEffectRouting(Core::GpuTaskSchedulingHint& scheduling){
    scheduling.allowCrossFamilySameClassQueueRouting = true;
}


[[nodiscard]] inline Core::GpuQueueRequest GraphicsComputeQueueRequest(){
    return Core::GpuQueueRequest{
        static_cast<Core::GpuQueueCapability::Mask>(
            static_cast<u8>(Core::GpuQueueCapability::Graphics)
            | static_cast<u8>(Core::GpuQueueCapability::Compute)
        ),
        Core::GpuQueuePreference::Graphics,
        false,
        false,
    };
}

// These callbacks dispatch Compute work but form one ordered packet with the graphics prefix and subsequent
// deferred passes. Keep the physical primary-Graphics route while declaring the command capability they use.
[[nodiscard]] inline Core::GpuQueueRequest GraphicsPreferredComputeQueueRequest(){
    return Core::GpuQueueRequest{
        Core::GpuQueueCapability::Compute,
        Core::GpuQueuePreference::Graphics,
        false,
        false,
    };
}

// Hybrid shadow preparation's software tail dispatches its per-mesh BVH work and may emit small direct buffer
// writes while restoring the established hardware fallback. It still belongs in the accepting primary-Graphics
// packet, but needs both capabilities declared for debug recording to validate the real callback.
[[nodiscard]] inline Core::GpuQueueRequest GraphicsComputeUploadQueueRequest(){
    return Core::GpuQueueRequest{
        static_cast<Core::GpuQueueCapability::Mask>(
            static_cast<u8>(Core::GpuQueueCapability::Transfer)
            | static_cast<u8>(Core::GpuQueueCapability::Compute)
        ),
        Core::GpuQueuePreference::Graphics,
        false,
        false,
    };
}

// A tiny setup dispatch can otherwise be rerouted to Graphics while its large Compute consumer selects the dedicated
// Compute transport. Use this only for work that must merge into that consumer's packet, so both requests select the
// same physical queue whenever a Compute transport is available.
[[nodiscard]] inline Core::GpuQueueRequest ComputePacketQueueRequest(){
    return Core::GpuQueueRequest{
        Core::GpuQueueCapability::Compute,
        Core::GpuQueuePreference::Compute,
        true,
        false,
    };
}

// Native image clears require Transfer capability, while Surfel GI keeps its output initialization and compute work
// in one packet on the selected Compute transport. Lock the Compute preference so a tiny clear does not fall back to
// Graphics merely because it is too small to amortize a queue crossing; Graphics remains the explicit fallback when
// no Compute transport exists.
[[nodiscard]] inline Core::GpuQueueRequest ComputeTransferQueueRequest(){
    return Core::GpuQueueRequest{
        Core::GpuQueueCapability::Transfer,
        Core::GpuQueuePreference::Compute,
        true,
        false,
    };
}

// Adaptive software-shadow primitives are built-in Transfer operations, but their following/preceding traversal
// callback dispatches Compute work.  Require both capabilities and lock the Compute preference so the whole
// clear -> trace -> readback chain selects one physical packet on every supported topology.
[[nodiscard]] inline Core::GpuQueueRequest ComputeTransferPacketQueueRequest(){
    return Core::GpuQueueRequest{
        static_cast<Core::GpuQueueCapability::Mask>(
            static_cast<u8>(Core::GpuQueueCapability::Compute)
            | static_cast<u8>(Core::GpuQueueCapability::Transfer)
        ),
        Core::GpuQueuePreference::Compute,
        true,
        false,
    };
}

// The lagged-history selector must share Deferred Lighting's dedicated Compute packet. Its built-in upload needs
// Transfer capability, which the Vulkan Compute transport also advertises, but may not be rerouted for its tiny cost.
[[nodiscard]] inline Core::GpuQueueRequest ComputeUploadQueueRequest(){
    return Core::GpuQueueRequest{
        Core::GpuQueueCapability::Transfer,
        Core::GpuQueuePreference::Compute,
        false,
        false,
    };
}

[[nodiscard]] inline Core::GpuQueueRequest TransferQueueRequest(){
    return Core::GpuQueueRequest{
        Core::GpuQueueCapability::Transfer,
        Core::GpuQueuePreference::Transfer,
        true,
        true,
    };
}

// The terminal graphics-prefix task publishes its ordinary and route-selected trace-geometry states before the
// following graph packets. All of those states are declared below, so this callback retains only timing ownership.


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

