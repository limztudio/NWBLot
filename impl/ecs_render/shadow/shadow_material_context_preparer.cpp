// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shadow_material_context_preparer.h"
#include <impl/ecs_render/raytrace/ray_trace_material_resources.h>

#include <impl/ecs_render/material/instance_typed_ranges.adhd.h>
#include <impl/ecs_render/raytrace/rt_swbvh_helpers.h>
#include <impl/ecs_render/shadow/slot_selection.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<u64> ShadowMaterialContextPreparer::prepare(
    const PreparedShadowMaterialContextRoute::Enum route,
    const b8 staticScene,
    const usize sceneInstanceCount,
    const Span<const NwbRtInstanceMaterialGpu> instanceMaterials,
    const Span<const InstanceGpuData> shadowInstanceData,
    MaterialTypedByteDataVector& shadowMaterialTypedBytes
){
    using namespace RayTracingSoftwareBvhDetail;

    if(route != PreparedShadowMaterialContextRoute::Hardware && route != PreparedShadowMaterialContextRoute::Software)
        return MakeUnexpected(Failure{});
    if(shadowMaterialTypedBytes.empty())
        shadowMaterialTypedBytes.resize(sizeof(u32), 0u);
    const usize materialTypedUploadBytes = ECSRenderDetail::ResolveMaterialTypedUploadByteCount(shadowMaterialTypedBytes);
    const u64 materialContextHash = ComputeShadowMaterialContextHash(instanceMaterials, shadowInstanceData, shadowMaterialTypedBytes);
    const b8 hardware = route == PreparedShadowMaterialContextRoute::Hardware;
    const b8 canReuseMaterialContext =
        staticScene
        && (!hardware || !m_system.m_rayTracingState.m_sceneHasTransparentOccluder)
        && (hardware ? m_system.m_rayTracingState.m_hwShadowMaterialContextHashValid : m_system.m_rayTracingState.m_swShadowMaterialContextHashValid)
        && (hardware ? m_system.m_rayTracingState.m_hwShadowMaterialContextHash : m_system.m_rayTracingState.m_swShadowMaterialContextHash) == materialContextHash
        && HasPreparedShadowMaterialContextBuffers(m_system.m_rayTracingState, instanceMaterials.size(), shadowInstanceData.size(), materialTypedUploadBytes)
    ;
    if(!hardware && canReuseMaterialContext && !m_system.capturePreparedShadowMaterialContextCacheReuse(materialContextHash, instanceMaterials.size(), shadowInstanceData.size(), materialTypedUploadBytes))
        return MakeUnexpected(Failure{});
    if(!canReuseMaterialContext){
        if(
            !RayTraceMaterialResources(m_system.m_graphics, m_system.m_rayTracingState, m_system.m_rayTracingState).ensureShadowInstanceMaterialBuffer(sceneInstanceCount)
            || !RayTraceMaterialResources(m_system.m_graphics, m_system.m_rayTracingState, m_system.m_rayTracingState).ensureShadowInstanceContextBuffer(shadowInstanceData.size())
            || !RayTraceMaterialResources(m_system.m_graphics, m_system.m_rayTracingState, m_system.m_rayTracingState).ensureShadowMaterialTypedBuffer(materialTypedUploadBytes)
            || !HasPreparedShadowMaterialContextBuffers(m_system.m_rayTracingState, instanceMaterials.size(), shadowInstanceData.size(), materialTypedUploadBytes)
        )
            return MakeUnexpected(Failure{});
        if(!m_system.capturePreparedShadowMaterialContext(route, staticScene, materialContextHash, instanceMaterials, shadowInstanceData, Span<const u8>(shadowMaterialTypedBytes.data(), materialTypedUploadBytes)))
            return MakeUnexpected(Failure{});
    }
    return materialContextHash;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

