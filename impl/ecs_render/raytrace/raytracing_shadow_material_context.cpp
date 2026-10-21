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
bool RendererRayTracingSystem::capturePreparedShadowMaterialContext(
    const PreparedShadowMaterialContextRoute route,
    const bool staticScene,
    const u64 hash,
    const void* const instanceMaterialData,
    const usize instanceMaterialCount,
    const usize instanceMaterialByteCount,
    const void* const instanceData,
    const usize instanceCount,
    const usize instanceByteCount,
    const void* const materialTypedData,
    const usize materialTypedByteCount
){
    if(!m_preparedShadowMaterialContext.capture(m_rayTracingState, route, staticScene, hash, instanceMaterialData, instanceMaterialCount, instanceMaterialByteCount, instanceData, instanceCount, instanceByteCount, materialTypedData, materialTypedByteCount))
        return false;
    clearPreparedSceneSwBvhTraversal();
    return true;
}

bool RendererRayTracingSystem::capturePreparedShadowMaterialContextCacheReuse(const u64 hash, const usize instanceMaterialCount, const usize instanceCount, const usize materialTypedByteCount)noexcept{
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

