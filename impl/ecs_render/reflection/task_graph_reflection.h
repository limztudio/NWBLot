// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "reflection_system.h"

#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererRayTracingSystem;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_ReflectionCsgHardwareSliceFamily("render.reflection.hardware.csg_slice");

[[nodiscard]] Expected<Name, ErrorCode> ReflectionCsgHardwareSliceIdentity(u32 slice);

struct ReflectionGraphInputs{
    RendererRayTracingSystem& raytracingSystem;
    Core::GpuGraphResourceId opaqueDepth;
    Core::GpuGraphResourceId opaqueColor;
    // Surface reads cover view/deferred selectors, F0/roughness, depth/normal, glass captures.
    Span<const Core::GpuTaskResourceUse> surfaceReads{};
    Span<const Core::GpuTaskResourceUse> hardwareReads{};
    Span<const Core::GpuTaskResourceSetUse> hardwareSetReads{};
    const bool* hardwarePreparationReady = nullptr;
    bool* hardwareDispatchLogged = nullptr;
    bool* fallbackDispatchLogged = nullptr;
};

struct ReflectionGraphResult{
    Core::GpuTaskId completion;
    Core::GpuGraphResourceId opaqueRadiance;
    Core::GpuGraphResourceId glassRadiance;
    Core::GpuGraphResourceId frameParameters;
    Core::GpuGraphResourceId counters;


public:
    [[nodiscard]] bool valid()const noexcept{ return completion.valid(); }
};

// Inputs copy at declaration; outcomes and latches are frame-owned past packet lifetime.
[[nodiscard]] Expected<ReflectionGraphResult> DeclareReflectionTasks(
    const ReflectionFrameSnapshot& resources,
    const ReflectionGraphInputs& inputs,
    Core::GpuTaskId dependency,
    Core::GpuTaskGraph& graph,
    Core::GraphicsRuntime& graphics,
    RendererReflectionSystem& reflectionSystem,
    Core::Alloc::ScratchArena& scratchArena
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

