// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "mesh_system.h"
#include "mesh_view_private.h"

#include <impl/ecs_render/mesh/renderer_mesh_state.h>

#include <core/common/log.h>
#include <core/graphics/runtime/runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


MeshViewGpuData ResolveMeshViewState(const f32 fallbackAspectRatio, Core::ECS::World& world)noexcept{
    SIMDMatrix viewBasis = NWB::Impl::Scene::BuildDefaultSceneViewBasis();
    NWB::Impl::Scene::CameraProjection projection = NWB::Impl::Scene::StoreDefaultCameraProjection(fallbackAspectRatio);

    const NWB::Impl::Scene::SceneCameraView cameraView = NWB::Impl::Scene::ResolveSceneCameraView(world, fallbackAspectRatio);
    if(cameraView.valid()){
        viewBasis = NWB::Impl::Scene::BuildSceneViewBasis(LoadFloat(cameraView.transform->position), LoadFloat(cameraView.transform->rotation));
        projection = cameraView.projection;
    }

    Float4 rightRow;
    Float4 upRow;
    Float4 forwardRow;
    Float4 positionDepthBias;
    StoreFloat(viewBasis.v[0], rightRow);
    StoreFloat(viewBasis.v[1], upRow);
    StoreFloat(viewBasis.v[2], forwardRow);
    StoreFloat(viewBasis.v[3], positionDepthBias);

    return BuildMeshViewGpuData(
        rightRow,
        upRow,
        forwardRow,
        positionDepthBias,
        projection.projectionParams,
        projection.tanHalfVerticalFov,
        projection.aspectRatio,
        projection.nearPlane,
        projection.farPlane
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererMeshState::invalidateResources()noexcept{
    m_meshes.clear();
    m_meshViewBuffer.reset();
    m_frameBindings = {};
    m_meshViewGpuDataValid = false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const Core::BufferHandle& RendererMeshSystem::MeshSourceBuffer(const MeshResources& mesh, const u32 bindingSlot)noexcept{
    switch(bindingSlot){
    case NWB_MESH_BINDING_POSITION: return mesh.positionBuffer;
    case NWB_MESH_BINDING_NORMAL: return mesh.normalBuffer;
    case NWB_MESH_BINDING_TANGENT: return mesh.tangentBuffer;
    case NWB_MESH_BINDING_UV0: return mesh.uv0Buffer;
    case NWB_MESH_BINDING_COLOR: return mesh.colorBuffer;
    case NWB_MESH_BINDING_MESHLET_DESC: return mesh.meshletDescBuffer;
    case NWB_MESH_BINDING_MESHLET_BOUNDS: return mesh.meshletBoundsBuffer;
    case NWB_MESH_BINDING_MESHLET_POSITION_REFS: return mesh.meshletPositionRefDeltaBuffer;
    case NWB_MESH_BINDING_MESHLET_ATTRIBUTE_REFS: return mesh.meshletAttributeRefDeltaBuffer;
    case NWB_MESH_BINDING_MESHLET_LOCAL_VERTEX_REFS: return mesh.meshletLocalVertexRefBuffer;
    case NWB_MESH_BINDING_MESHLET_PRIMITIVE_INDICES: return mesh.meshletPrimitiveIndexBuffer;
    default:
        NWB_ASSERT(false);
        return mesh.positionBuffer;
    }
}

void RendererMeshSystem::invalidateResources(){
    releaseAllMeshGeometryHeapHandles();
    releaseMeshFrameHeapHandles();
    m_meshState.invalidateResources();
}

b8 RendererMeshSystem::createMeshViewBuffer(){
    if(m_meshState.m_meshViewBuffer)
        return true;

    Core::BufferDesc meshViewBufferDesc;
    meshViewBufferDesc
        .setByteSize(sizeof(MeshViewGpuData))
        .setIsConstantBuffer(true)
        .setDebugName(s_MeshViewBufferName)
        // Either caustic producer can consume the view; keep concurrent to avoid serializing lanes.
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute).enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    auto meshViewBufferResult = m_graphics.createBuffer(meshViewBufferDesc);
    if(!meshViewBufferResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create mesh view buffer"));
        return false;
    }
    Core::BufferHandle meshViewBuffer = Move(*meshViewBufferResult);

    releaseMeshFrameHeapHandles();
    m_meshState.m_meshViewBuffer = Move(meshViewBuffer);
    return true;
}

ECSRenderDetail::MeshViewBufferSnapshot RendererMeshSystem::meshViewBufferSnapshot()const noexcept{
    ECSRenderDetail::MeshViewBufferSnapshot snapshot;
    snapshot.buffer = m_meshState.m_meshViewBuffer;
    if(m_meshState.m_frameBindings.meshView.buffer == m_meshState.m_meshViewBuffer)
        snapshot.heapHandle = m_meshState.m_frameBindings.meshView.heapHandle;
    return snapshot;
}

Expected<Float44> RendererMeshSystem::snapshotAcceptedMeshViewWorldToClip()const noexcept{
    if(!m_meshState.m_meshViewGpuDataValid)
        return MakeUnexpected(Failure{});

    MeshViewGpuData acceptedView;
    NWB_MEMCPY(&acceptedView, sizeof(acceptedView), m_meshState.m_meshViewGpuData.data(), m_meshState.m_meshViewGpuData.size());
    return acceptedView.worldToClip;
}

MeshViewBufferUpload RendererMeshSystem::prepareMeshViewBufferUpload(const f32 fallbackAspectRatio)const noexcept{
    NWB_ASSERT(m_meshState.m_meshViewBuffer);

    MeshViewBufferUpload upload;
    upload.viewState = ECSRenderDetail::ResolveMeshViewState(fallbackAspectRatio, m_world);
    upload.uploadRequired = !(m_meshState.m_meshViewGpuDataValid && NWB_MEMCMP(m_meshState.m_meshViewGpuData.data(), &upload.viewState, sizeof(upload.viewState)) == 0);
    return upload;
}

void RendererMeshSystem::confirmMeshViewBufferUpload(const MeshViewGpuData& viewState)noexcept{
    NWB_MEMCPY(m_meshState.m_meshViewGpuData.data(), m_meshState.m_meshViewGpuData.size(), &viewState, sizeof(viewState));
    m_meshState.m_meshViewGpuDataValid = true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

