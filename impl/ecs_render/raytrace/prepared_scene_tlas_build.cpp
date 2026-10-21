// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "prepared_scene_tlas_build.h"

#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>

#include <core/graphics/backend_selection/backend.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool PreparedSceneTlasBuild::capture(
    const RendererRayTracingState& state,
    const bool staticScene,
    const u64 staticSceneHash,
    const Vector<Core::RayTracingInstanceDesc, Core::Alloc::ScratchArena>& instances,
    const Vector<Core::RayTracingAccelStructHandle, Core::Alloc::ScratchArena>& instanceBlases
){
    clear();
    if(
        instances.empty()
        || instances.size() != instanceBlases.size()
        || !state.m_tlas
        || !state.m_tlas->getBackingBufferHandle()
        || state.m_tlasMaxInstances < instances.size()
        || !state.m_tlasHeapHandle.valid()
        || state.m_tlasHeapHandle.descriptorClass() != Core::GpuDescriptorClass::AccelStruct
    )
        return false;

    m_instances.reserve(instances.size());
    m_blases.reserve(instanceBlases.size());
    for(usize index = 0u; index < instances.size(); ++index){
        const Core::RayTracingAccelStructHandle& blas = instanceBlases[index];
        if(!blas || instances[index].bottomLevelAS != blas.get()){
            clear();
            return false;
        }
        m_instances.push_back(instances[index]);
        m_blases.push_back(blas);
    }
    m_tlas = state.m_tlas;
    m_backingBuffer = state.m_tlas->getBackingBufferHandle();
    m_heapHandle = state.m_tlasHeapHandle;
    m_maxInstances = state.m_tlasMaxInstances;
    m_staticSceneHash = staticSceneHash;
    m_static = staticScene;
    m_ready = true;
    return true;
}

bool PreparedSceneTlasBuild::record(RendererRayTracingState& state, Core::CommandList& commandList){
    if(
        !m_ready
        || m_instances.empty()
        || m_instances.size() != m_blases.size()
        || state.m_tlas.get() != m_tlas.get()
        || !state.m_tlas
        || state.m_tlas->getBackingBufferHandle().get() != m_backingBuffer.get()
        || state.m_tlasMaxInstances != m_maxInstances
        || state.m_tlasHeapHandle != m_heapHandle
        || !state.m_tlasHeapHandle.valid()
        || state.m_tlasHeapHandle.descriptorClass() != Core::GpuDescriptorClass::AccelStruct
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen scene TLAS build no longer matches preflight storage"));
        return false;
    }
    for(usize index = 0u; index < m_instances.size(); ++index){
        if(!m_blases[index] || m_instances[index].bottomLevelAS != m_blases[index].get()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen scene TLAS build lost a referenced BLAS"));
            return false;
        }
    }

    commandList.buildTopLevelAccelStruct(m_tlas.get(), m_instances.data(), m_instances.size(), Core::RayTracingAccelStructBuildFlags::PreferFastTrace);
    state.m_tlasDeviceAddress = m_tlas->getDeviceAddress();
    return true;
}

void PreparedSceneTlasBuild::confirm(RendererRayTracingState& state)noexcept{
    if(!m_ready)
        return;

    const bool preparedTlasMatchesCurrent =
        state.m_tlas.get() == m_tlas.get()
        && state.m_tlas
        && state.m_tlas->getBackingBufferHandle().get() == m_backingBuffer.get()
        && state.m_tlasMaxInstances == m_maxInstances
        && state.m_tlasHeapHandle == m_heapHandle
    ;
    if(preparedTlasMatchesCurrent && m_static){
        state.m_tlasStaticSceneHash = m_staticSceneHash;
        state.m_tlasStaticSceneHashValid = true;
    }
    else
        state.m_tlasStaticSceneHashValid = false;
    // This callback follows the accepted persistent-state handoff. A later graph import must use that binding rather than reasserting Common for this backing generation.
    if(preparedTlasMatchesCurrent){
        state.m_tlasBackingFresh = false;
    }
    clear();
}

void PreparedSceneTlasBuild::clear()noexcept{
    m_instances.clear();
    m_blases.clear();
    m_tlas = nullptr;
    m_backingBuffer = nullptr;
    m_heapHandle = Core::GpuDescriptorHandle::Invalid();
    m_maxInstances = 0u;
    m_staticSceneHash = 0u;
    m_static = false;
    m_ready = false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

