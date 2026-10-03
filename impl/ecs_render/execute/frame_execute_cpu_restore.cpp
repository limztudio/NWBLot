// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/execute/frame_execute_cpu_restore.h>
#include <impl/ecs_render/avboit/avboit_system.h>
#include <impl/ecs_render/deferred/deferred_system.h>
#include <impl/ecs_render/mesh/mesh_system.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void FrameExecuteCpuRestore::restorePrefixCpuState(
    RendererMeshSystem& meshSystem,
    RendererDeferredSystem& deferredSystem
){
    // Rejected recording invalidates upload mirrors.
    meshSystem.invalidateMeshViewBufferUploadMirror();
    deferredSystem.invalidateSceneLightingUploadMirrors();
}


void FrameExecuteCpuRestore::restoreShadowCpuState(
    RendererRayTracingState& rayTracingState,
    const RayTracingFrameCpuStateSnapshot& snapshot
){
    rayTracingState.restoreShadowPacketCpuState(snapshot);
}


void FrameExecuteCpuRestore::restoreCausticsCpuState(
    RendererRayTracingState& rayTracingState,
    const RayTracingFrameCpuStateSnapshot& snapshot
){
    rayTracingState.restoreCausticPacketCpuState(snapshot);
}


void FrameExecuteCpuRestore::restoreSurfelGiCpuState(
    RendererRayTracingState& rayTracingState,
    const RayTracingFrameCpuStateSnapshot& snapshot
){
    rayTracingState.restoreSurfelGiPacketCpuState(snapshot);
}


void FrameExecuteCpuRestore::restoreAvboitCpuState(
    RendererAvboitSystem& avboitSystem,
    const bool targetsNeedClear
){
    avboitSystem.restoreTargetClearState(targetsNeedClear);
}


void FrameExecuteCpuRestore::restorePostGbufferEffectsCpuState(
    RendererRayTracingState& rayTracingState,
    RendererAvboitSystem& avboitSystem,
    const RayTracingFrameCpuStateSnapshot& snapshot,
    const bool avboitTargetsNeedClear
){
    restoreCausticsCpuState(rayTracingState, snapshot);
    restoreSurfelGiCpuState(rayTracingState, snapshot);
    restoreAvboitCpuState(avboitSystem, avboitTargetsNeedClear);
}


void FrameExecuteCpuRestore::restorePostGbufferPacketCpuState(
    RendererMeshSystem& meshSystem,
    RendererDeferredSystem& deferredSystem,
    RendererRayTracingState& rayTracingState,
    RendererAvboitSystem& avboitSystem,
    DeferredFrameTargets& targets,
    const Snapshot& snapshot,
    const bool restoreBindlessSlots
){
    if(restoreBindlessSlots)
        targets.bindless.slotsUploaded = snapshot.deferredBindlessSlotsUploaded;
    restorePrefixCpuState(meshSystem, deferredSystem);
    restoreShadowCpuState(rayTracingState, snapshot.rayTracing);
    restorePostGbufferEffectsCpuState(rayTracingState, avboitSystem, snapshot.rayTracing, snapshot.avboitTargetsNeedClear);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END
