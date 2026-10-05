// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/renderer_frame_pipeline.h>
#include <impl/ecs_render/mesh/mesh_system.h>
#include <impl/ecs_render/deferred/deferred_system.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <impl/ecs_render/avboit/avboit_system.h>
#include <impl/ecs_render/raytrace/graph_snapshots.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererFramePipeline;
struct DeferredFrameTargets;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// CPU-side packet-state restore for rejected frame recordings.
// Separated from RendererFramePipeline::render(): prefix/shadow/effects restores are
// per-stage functionality, composed here so render() keeps only the call sites.
class FrameExecuteCpuRestore final{
public:
    FrameExecuteCpuRestore() = delete;


    struct Snapshot{
        RayTracingFrameCpuStateSnapshot rayTracing;
        bool avboitTargetsNeedClear = false;
        bool deferredBindlessSlotsUploaded = false;
    };


    static void RestorePrefixCpuState(
        RendererMeshSystem& meshSystem,
        RendererDeferredSystem& deferredSystem
    );
    static void RestoreShadowCpuState(
        RendererRayTracingState& rayTracingState,
        const RayTracingFrameCpuStateSnapshot& snapshot
    );
    static void RestoreCausticsCpuState(
        RendererRayTracingState& rayTracingState,
        const RayTracingFrameCpuStateSnapshot& snapshot
    );
    static void RestoreSurfelGiCpuState(
        RendererRayTracingState& rayTracingState,
        const RayTracingFrameCpuStateSnapshot& snapshot
    );
    static void RestoreAvboitCpuState(
        RendererAvboitSystem& avboitSystem,
        bool targetsNeedClear
    );
    static void RestorePostGbufferEffectsCpuState(
        RendererRayTracingState& rayTracingState,
        RendererAvboitSystem& avboitSystem,
        const RayTracingFrameCpuStateSnapshot& snapshot,
        bool avboitTargetsNeedClear
    );
    static void RestorePostGbufferPacketCpuState(
        RendererMeshSystem& meshSystem,
        RendererDeferredSystem& deferredSystem,
        RendererRayTracingState& rayTracingState,
        RendererAvboitSystem& avboitSystem,
        DeferredFrameTargets& targets,
        const Snapshot& snapshot,
        bool restoreBindlessSlots
    );
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END
