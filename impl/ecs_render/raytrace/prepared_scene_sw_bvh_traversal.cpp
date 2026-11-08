// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "prepared_scene_sw_bvh_traversal.h"
#include "raytracing_system.h"

#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <impl/ecs_render/raytrace/rt_private.h>

#include <global/overflow.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 PreparedSceneSwBvhTraversal::capture(const Span<const PreparedSceneSwBvhMesh> meshes, const u32 instanceCount){
    clear();
    const usize meshCount = meshes.size();
    if(
        meshCount == 0u
        || meshCount > static_cast<usize>(Limit<u32>::s_Max)
        || instanceCount == 0u
        || !m_owner.m_preparedSceneBvh.covers(instanceCount)
        || !m_owner.m_preparedShadowMaterialContext.softwareSnapshotCovers(instanceCount)
        || !m_owner.preparedTraversalStorageMatchesCurrent()
    )
        return false;

    m_preparedSceneSwBvhMeshes.reserve(meshCount);
    for(usize index = 0u; index < meshCount; ++index){
        const PreparedSceneSwBvhMesh& mesh = meshes[index];
        if(
            mesh.meshName == s_NameNone
            || !mesh.nodeBuffer
            || !mesh.positionBuffer
            || !mesh.triangleIndexBuffer
            || !mesh.attributeBuffer
            || !RayTracingDetail::IsHeapHandle(mesh.nodeHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
            || !RayTracingDetail::IsHeapHandle(mesh.positionHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
            || !RayTracingDetail::IsHeapHandle(mesh.triangleIndexHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
            || !RayTracingDetail::IsHeapHandle(mesh.attributeHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
            || mesh.primitiveCount == 0u
            || mesh.nodeByteSize == 0u
            || mesh.positionByteSize == 0u
            || mesh.triangleIndexByteSize == 0u
            || mesh.attributeByteSize == 0u
        ){
            clear();
            return false;
        }
        m_preparedSceneSwBvhMeshes.push_back(mesh);
    }
    m_preparedSceneSwBvhInstanceCount = instanceCount;
    m_preparedSceneSwBvhRendererMutationVersion = m_owner.m_world.componentMutationVersion<RendererComponent>();
    m_preparedSceneSwBvhTransformMutationVersion = m_owner.m_world.componentMutationVersion<NWB::Impl::Scene::TransformComponent>();
    m_preparedSceneSwBvhMaterialMutationVersion = m_owner.m_world.componentMutationVersion<MaterialInstanceComponent>();
    m_preparedSceneSwBvhReady = true;
    return true;
}

b8 PreparedSceneSwBvhTraversal::record(){
    auto& state = m_owner.m_rayTracingState;
    const b8 sceneSnapshotValid = m_owner.m_preparedSceneBvh.snapshotAccepted(state);
    const b8 materialSnapshotValid = m_owner.m_preparedShadowMaterialContext.softwareSnapshotAccepted(state);
    const auto rejectPreparedTraversal = [&]()noexcept{
        if(!m_owner.m_preparedSceneBvh.uploadRequired())
            state.m_sceneSwBvhStaticSceneHashValid = false;
        if(!m_owner.m_preparedShadowMaterialContext.uploadRequired())
            state.m_swShadowMaterialContextHashValid = false;
        return false;
    };
    if(
        !m_preparedSceneSwBvhReady
        || m_preparedSceneSwBvhMeshes.empty()
        || m_preparedSceneSwBvhInstanceCount == 0u
        || !m_owner.m_preparedSceneBvh.covers(m_preparedSceneSwBvhInstanceCount)
        || !m_owner.m_preparedShadowMaterialContext.softwareSnapshotCovers(m_preparedSceneSwBvhInstanceCount)
        || !sceneSnapshotValid
        || !materialSnapshotValid
        || !m_owner.preparedTraversalStorageMatchesCurrent()
        || !RayTracingDetail::IsHeapHandle(state.m_sceneBvhNodeHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !RayTracingDetail::IsHeapHandle(state.m_sceneInstanceHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !RayTracingDetail::IsHeapHandle(state.m_shadowInstanceMaterialHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !RayTracingDetail::IsHeapHandle(state.m_shadowInstanceHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !RayTracingDetail::IsHeapHandle(state.m_shadowMaterialTypedHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen software scene traversal no longer matches preflight storage"));
        return rejectPreparedTraversal();
    }
    if(
        m_owner.m_world.componentMutationVersion<RendererComponent>() != m_preparedSceneSwBvhRendererMutationVersion
        || m_owner.m_world.componentMutationVersion<NWB::Impl::Scene::TransformComponent>() != m_preparedSceneSwBvhTransformMutationVersion
        || m_owner.m_world.componentMutationVersion<MaterialInstanceComponent>() != m_preparedSceneSwBvhMaterialMutationVersion
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen software scene inputs changed after graph preflight"));
        return rejectPreparedTraversal();
    }

    const auto matchesMesh = [this](const ECSRenderDetail::MeshRayTracingResourceSnapshot& mesh, const PreparedSceneSwBvhMesh& prepared)noexcept{
        const auto attributeCache = m_owner.m_rayTracingState.m_swMeshHeapHandleCache.find(NotNull<const Core::Buffer*>(prepared.attributeBuffer.get()));
        const b8 topologyReady = mesh.swBvhTopologyBuilt || m_owner.preparedMeshSwBvhBuildProducesTopology(mesh);
        return
            mesh.meshName == prepared.meshName
            && mesh.runtimeMesh == prepared.runtimeMesh
            && mesh.runtimeMeshVersion == prepared.runtimeMeshVersion
            && mesh.runtimeGeometryContentRevision == prepared.geometryContentRevision
            && mesh.swBvhNodeBuffer.get() == prepared.nodeBuffer.get()
            && mesh.positionBuffer.get() == prepared.positionBuffer.get()
            && mesh.triangleIndexBuffer.get() == prepared.triangleIndexBuffer.get()
            && mesh.attributeBuffer.get() == prepared.attributeBuffer.get()
            && mesh.swBvhNodeHeapHandle == prepared.nodeHeapHandle
            && mesh.swBvhPositionHeapHandle == prepared.positionHeapHandle
            && mesh.swBvhTriangleIndexHeapHandle == prepared.triangleIndexHeapHandle
            && attributeCache != m_owner.m_rayTracingState.m_swMeshHeapHandleCache.end()
            && attributeCache.value().handle == prepared.attributeHeapHandle
            && topologyReady
            && mesh.meshletPrimitiveIndexCount == prepared.primitiveCount * s_RayTracingTriangleIndexCount
            && mesh.swBvhNodeBuffer->getCreationDescription().byteSize == prepared.nodeByteSize
            && mesh.positionBuffer->getCreationDescription().byteSize == prepared.positionByteSize
            && mesh.triangleIndexBuffer->getCreationDescription().byteSize == prepared.triangleIndexByteSize
            && mesh.attributeBuffer->getCreationDescription().byteSize == prepared.attributeByteSize
            && RayTracingDetail::IsHeapHandle(prepared.nodeHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
            && RayTracingDetail::IsHeapHandle(prepared.positionHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
            && RayTracingDetail::IsHeapHandle(prepared.triangleIndexHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
            && RayTracingDetail::IsHeapHandle(prepared.attributeHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        ;
    };

    for(const PreparedSceneSwBvhMesh& prepared : m_preparedSceneSwBvhMeshes){
        auto meshResult = m_owner.m_meshSystem.findRayTracingResourceSnapshot(prepared.meshName);
        if(!meshResult || !matchesMesh((*meshResult), prepared)){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen software scene lost mesh '{}'"), ConvertString(prepared.meshName.resolvedText()));
            return rejectPreparedTraversal();
        }
    }

    const b8 tablesMatch =
        state.m_swShadowMeshCount == static_cast<u32>(m_preparedSceneSwBvhMeshes.size())
        && state.m_sceneBvhInstanceCount == m_preparedSceneSwBvhInstanceCount
        && state.m_swShadowMeshNodeBuffers.size() == m_preparedSceneSwBvhMeshes.size()
        && state.m_swShadowMeshPositionBuffers.size() == m_preparedSceneSwBvhMeshes.size()
        && state.m_swShadowMeshIndexBuffers.size() == m_preparedSceneSwBvhMeshes.size()
        && state.m_swShadowMeshAttributeBuffers.size() == m_preparedSceneSwBvhMeshes.size()
        && state.m_swShadowMeshNodeHandles.size() == m_preparedSceneSwBvhMeshes.size()
        && state.m_swShadowMeshPositionHandles.size() == m_preparedSceneSwBvhMeshes.size()
        && state.m_swShadowMeshIndexHandles.size() == m_preparedSceneSwBvhMeshes.size()
        && state.m_swShadowMeshAttributeHandles.size() == m_preparedSceneSwBvhMeshes.size()
    ;
    if(!tablesMatch){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen software scene traversal table changed after graph preflight"));
        return rejectPreparedTraversal();
    }
    for(usize index = 0u; index < m_preparedSceneSwBvhMeshes.size(); ++index){
        const PreparedSceneSwBvhMesh& prepared = m_preparedSceneSwBvhMeshes[index];
        if(
            state.m_swShadowMeshNodeBuffers[index].get() != prepared.nodeBuffer.get()
            || state.m_swShadowMeshPositionBuffers[index].get() != prepared.positionBuffer.get()
            || state.m_swShadowMeshIndexBuffers[index].get() != prepared.triangleIndexBuffer.get()
            || state.m_swShadowMeshAttributeBuffers[index].get() != prepared.attributeBuffer.get()
            || state.m_swShadowMeshNodeHandles[index] != prepared.nodeHeapHandle
            || state.m_swShadowMeshPositionHandles[index] != prepared.positionHeapHandle
            || state.m_swShadowMeshIndexHandles[index] != prepared.triangleIndexHeapHandle
            || state.m_swShadowMeshAttributeHandles[index] != prepared.attributeHeapHandle
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen software scene traversal table no longer matches preflight"));
            return rejectPreparedTraversal();
        }
    }
    return true;
}

void PreparedSceneSwBvhTraversal::clear()noexcept{
    m_preparedSceneSwBvhMeshes.clear();
    m_preparedSceneSwBvhInstanceCount = 0u;
    m_preparedSceneSwBvhRendererMutationVersion = 0u;
    m_preparedSceneSwBvhTransformMutationVersion = 0u;
    m_preparedSceneSwBvhMaterialMutationVersion = 0u;
    m_preparedSceneSwBvhReady = false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

