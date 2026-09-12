// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "smoke_environment.h"

#include <core/perf/report.h>
#include <global/filesystem.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// One sample is one published successful graphics frame, never an onUpdate call or publication-window average.
class RendererGatherBenchmarkProbe final : NoCopy{
public:
    static constexpr u32 s_WarmupFrames = 96u;
    static constexpr u32 s_SampleFrames = 256u;
    static constexpr u32 s_DrainFrames = 32u;
    static constexpr u32 s_CpuScopeCount = 8u;
    static constexpr u32 s_GpuScopeCount = 18u;
    static constexpr u32 s_ArenaScopeCount = 9u;
    static constexpr const char* s_CpuNames[s_CpuScopeCount] = {
        "graphics.frame", "graphics.frame_preamble", "graphics.prepare_resources", "graphics.render_passes",
        "graphics.render", "frame.project_update", "graphics.present", "graphics.begin_frame",
    };
    static constexpr const char* s_GpuNames[s_GpuScopeCount] = {
        "render.frame", "render.opaque_regular", "render.shadow_visibility", "render.deferred_lighting",
        "render.deferred_composite", "render.deferred_present", "render.avboit_clear", "render.avboit_occupancy",
        "render.avboit_depth_warp", "render.avboit_extinction", "render.avboit_integration", "render.avboit_accumulate",
        "render.reflection_classify", "render.reflection_build_args", "render.reflection_hardware",
        "render.reflection_depth_pyramid", "render.reflection_temporal", "render.reflection_spatial",
    };
    static constexpr const char* s_ArenaNames[s_ArenaScopeCount] = {
        "impl/ecs_render/prepare", "impl/ecs_render/render", "impl/ecs_render/task_graph",
        "impl/ecs_render/avboit_transparent_csg", "impl/ecs_render/material_pass_prepare",
        "impl/ecs_render/material_pass_render", "impl/ecs_render/material_instance_mutable",
        "impl/ecs_render/ray_tracing_build", "impl/ecs_render/ray_tracing_attribute",
    };


private:
    struct CpuSample{
        u64 sourceFrame = 0u;
        u64 publishFrame = 0u;
        Array<f64, s_CpuScopeCount> milliseconds = {};
        Array<Core::Perf::MemorySnapshot, s_ArenaScopeCount> memory = {};
    };
    struct GpuSample{
        Core::Perf::TimingStats timing;
        u32 scope = 0u;
    };


public:
    explicit RendererGatherBenchmarkProbe(Core::Alloc::GlobalArena& arena);

    [[nodiscard]] bool poll(const Core::Perf::SessionReport& report, bool memoryEnabled);

    [[nodiscard]] bool finished()const{ return m_successfulFrames >= s_WarmupFrames + s_SampleFrames + s_DrainFrames; }
    [[nodiscard]] u32 successfulFrames()const{ return m_successfulFrames; }

    [[nodiscard]] bool write(NotNull<const char*> path, NotNull<const char*> workload, bool memoryEnabled, u32 renderers,
        u32 runtimeRenderers, u32 transparentRenderers, u32 runtimeOwners)const;


private:
    static void writeMemory(OutputFileStream& output, const Array<Core::Perf::MemorySnapshot, s_ArenaScopeCount>& snapshots);


private:
    Vector<CpuSample, Core::Alloc::GlobalArena> m_cpu;
    Vector<GpuSample, Core::Alloc::GlobalArena> m_gpu;
    Array<u64, s_GpuScopeCount> m_gpuPublish = {};
    Array<bool, s_GpuScopeCount> m_hasGpuPublication = {};
    Array<Core::Perf::MemorySnapshot, s_ArenaScopeCount> m_memoryBaseline = {};
    u64 m_lastPublish = 0u;
    u32 m_successfulFrames = 0u;
    bool m_hasPublication = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

