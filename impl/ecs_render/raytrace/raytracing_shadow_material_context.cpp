// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "raytracing_system.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<PreparedShadowMaterialContextUploads> RendererRayTracingSystem::retainPreparedShadowMaterialContextUploads(Core::GpuTaskGraph& graph)const{
    return m_preparedShadowMaterialContext.retainUploads(m_rayTracingState, graph);
}

void RendererRayTracingSystem::confirmPreparedShadowMaterialContextUploads()noexcept{
    if(m_preparedShadowMaterialContext.publishAcceptedUpload(m_rayTracingState))
        clearPreparedShadowMaterialContext();
}

// The frozen software traversal indexes this context, so a new context always drops it.
b8 RendererRayTracingSystem::capturePreparedShadowMaterialContext(
    const PreparedShadowMaterialContextRoute::Enum route,
    const b8 staticScene,
    const u64 hash,
    const Span<const NwbRtInstanceMaterialGpu> instanceMaterials,
    const Span<const InstanceGpuData> instances,
    const Span<const u8> materialTypedBytes
){
    if(!m_preparedShadowMaterialContext.capture(m_rayTracingState, route, staticScene, hash, instanceMaterials, instances, materialTypedBytes))
        return false;
    clearPreparedSceneSwBvhTraversal();
    return true;
}

b8 RendererRayTracingSystem::capturePreparedShadowMaterialContextCacheReuse(const u64 hash, const usize instanceMaterialCount, const usize instanceCount, const usize materialTypedByteCount)noexcept{
    clearPreparedSceneSwBvhTraversal();
    return m_preparedShadowMaterialContext.captureCacheReuse(m_rayTracingState, hash, instanceMaterialCount, instanceCount, materialTypedByteCount);
}

void RendererRayTracingSystem::clearPreparedShadowMaterialContext()noexcept{
    m_preparedShadowMaterialContext.clear();
    clearPreparedSceneSwBvhTraversal();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

