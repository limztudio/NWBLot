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


bool RendererMeshSystem::createMeshViewBuffer(){
    if(m_meshState.m_meshViewBuffer)
        return true;

    Core::BufferDesc meshViewBufferDesc;
    meshViewBufferDesc
        .setByteSize(sizeof(ECSRenderDetail::MeshViewGpuData))
        .setIsConstantBuffer(true)
        .setDebugName(ECSRenderDetail::s_MeshViewBufferName)
        // Either caustic producer can consume the view; keep concurrent to avoid serializing lanes.
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    Core::BufferHandle meshViewBuffer = m_graphics.createBuffer(meshViewBufferDesc);
    if(!meshViewBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create mesh view buffer"));
        return false;
    }

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

    ECSRenderDetail::MeshViewGpuData acceptedView;
    NWB_MEMCPY(&acceptedView, sizeof(acceptedView), m_meshState.m_meshViewGpuData, sizeof(m_meshState.m_meshViewGpuData));
    return acceptedView.worldToClip;
}

ECSRenderDetail::MeshViewBufferUpload RendererMeshSystem::prepareMeshViewBufferUpload(
    const f32 fallbackAspectRatio
)const{
    NWB_ASSERT(m_meshState.m_meshViewBuffer);

    ECSRenderDetail::MeshViewBufferUpload upload;
    upload.viewState = ECSRenderDetail::ResolveMeshViewState(m_world, fallbackAspectRatio);
    upload.uploadRequired = !(
        m_meshState.m_meshViewGpuDataValid
        && NWB_MEMCMP(m_meshState.m_meshViewGpuData, &upload.viewState, sizeof(upload.viewState)) == 0
    );
    return upload;
}

void RendererMeshSystem::confirmMeshViewBufferUpload(const ECSRenderDetail::MeshViewGpuData& viewState){
    NWB_MEMCPY(m_meshState.m_meshViewGpuData, sizeof(m_meshState.m_meshViewGpuData), &viewState, sizeof(viewState));
    m_meshState.m_meshViewGpuDataValid = true;
}

void RendererMeshSystem::invalidateMeshViewBufferUploadMirror()noexcept{
    m_meshState.m_meshViewGpuDataValid = false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

