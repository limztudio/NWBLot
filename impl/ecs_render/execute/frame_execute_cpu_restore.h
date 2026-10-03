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


    static void restorePrefixCpuState(
        RendererMeshSystem& meshSystem,
        RendererDeferredSystem& deferredSystem
    );
    static void restoreShadowCpuState(
        RendererRayTracingState& rayTracingState,
        const RayTracingFrameCpuStateSnapshot& snapshot
    );
    static void restoreCausticsCpuState(
        RendererRayTracingState& rayTracingState,
        const RayTracingFrameCpuStateSnapshot& snapshot
    );
    static void restoreSurfelGiCpuState(
        RendererRayTracingState& rayTracingState,
        const RayTracingFrameCpuStateSnapshot& snapshot
    );
    static void restoreAvboitCpuState(
        RendererAvboitSystem& avboitSystem,
        bool targetsNeedClear
    );
    static void restorePostGbufferEffectsCpuState(
        RendererRayTracingState& rayTracingState,
        RendererAvboitSystem& avboitSystem,
        const RayTracingFrameCpuStateSnapshot& snapshot,
        bool avboitTargetsNeedClear
    );
    static void restorePostGbufferPacketCpuState(
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
