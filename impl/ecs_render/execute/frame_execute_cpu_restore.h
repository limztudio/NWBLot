// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/avboit/avboit_system.h>
#include <impl/ecs_render/deferred/deferred_system.h>
#include <impl/ecs_render/mesh/mesh_system.h>
#include <impl/ecs_render/raytrace/graph_snapshots.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <impl/ecs_render/deferred/deferred_frame_targets.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// CPU-side packet-state restore for rejected frame recordings.
// Stage restores live here so render() keeps only the call sites.
class FrameExecuteCpuRestore final{
public:
    struct Snapshot{
        RayTracingFrameCpuStateSnapshot rayTracing;
        b8 avboitTargetsNeedClear = false;
        b8 deferredBindlessSlotsUploaded = false;
    };


public:
    static void RestorePrefixCpuState(RendererMeshSystem& meshSystem, RendererDeferredSystem& deferredSystem)noexcept;
    static void RestorePostGbufferPacketCpuState(
        const Snapshot& snapshot,
        b8 restoreBindlessSlots,
        RendererMeshSystem& meshSystem,
        RendererDeferredSystem& deferredSystem,
        RendererRayTracingState& rayTracingState,
        RendererAvboitSystem& avboitSystem,
        DeferredFrameTargets& targets
    )noexcept;


public:
    FrameExecuteCpuRestore() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

