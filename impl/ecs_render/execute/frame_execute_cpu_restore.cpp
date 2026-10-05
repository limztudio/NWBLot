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


void FrameExecuteCpuRestore::RestorePrefixCpuState(
    RendererMeshSystem& meshSystem,
    RendererDeferredSystem& deferredSystem
){
    // Rejected recording invalidates upload mirrors.
    meshSystem.invalidateMeshViewBufferUploadMirror();
    deferredSystem.invalidateSceneLightingUploadMirrors();
}


void FrameExecuteCpuRestore::RestoreShadowCpuState(
    RendererRayTracingState& rayTracingState,
    const RayTracingFrameCpuStateSnapshot& snapshot
){
    rayTracingState.restoreShadowPacketCpuState(snapshot);
}


void FrameExecuteCpuRestore::RestoreCausticsCpuState(
    RendererRayTracingState& rayTracingState,
    const RayTracingFrameCpuStateSnapshot& snapshot
){
    rayTracingState.restoreCausticPacketCpuState(snapshot);
}


void FrameExecuteCpuRestore::RestoreSurfelGiCpuState(
    RendererRayTracingState& rayTracingState,
    const RayTracingFrameCpuStateSnapshot& snapshot
){
    rayTracingState.restoreSurfelGiPacketCpuState(snapshot);
}


void FrameExecuteCpuRestore::RestoreAvboitCpuState(
    RendererAvboitSystem& avboitSystem,
    const bool targetsNeedClear
){
    avboitSystem.restoreTargetClearState(targetsNeedClear);
}


void FrameExecuteCpuRestore::RestorePostGbufferEffectsCpuState(
    RendererRayTracingState& rayTracingState,
    RendererAvboitSystem& avboitSystem,
    const RayTracingFrameCpuStateSnapshot& snapshot,
    const bool avboitTargetsNeedClear
){
    RestoreCausticsCpuState(rayTracingState, snapshot);
    RestoreSurfelGiCpuState(rayTracingState, snapshot);
    RestoreAvboitCpuState(avboitSystem, avboitTargetsNeedClear);
}


void FrameExecuteCpuRestore::RestorePostGbufferPacketCpuState(
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
    RestorePrefixCpuState(meshSystem, deferredSystem);
    RestoreShadowCpuState(rayTracingState, snapshot.rayTracing);
    RestorePostGbufferEffectsCpuState(rayTracingState, avboitSystem, snapshot.rayTracing, snapshot.avboitTargetsNeedClear);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END
