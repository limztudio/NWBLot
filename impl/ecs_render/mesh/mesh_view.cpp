// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "mesh_system.h"
#include "mesh_view_private.h"

#include <impl/ecs_render/mesh/renderer_mesh_state.h>

#include <core/common/log.h>
#include <core/graphics/module.h>


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
        // Either caustic producer can consume the just-uploaded view on AsyncCompute after the Graphics prefix.
        // Keep this input concurrent so that dependency is a timeline wait, not an ownership transfer that serializes
        // the two lanes.
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

ECSRenderDetail::MeshViewBufferSnapshot RendererMeshSystem::meshViewBufferSnapshot()const{
    ECSRenderDetail::MeshViewBufferSnapshot snapshot;
    snapshot.buffer = m_meshState.m_meshViewBuffer;
    if(m_meshState.m_frameBindings.meshView.buffer == m_meshState.m_meshViewBuffer)
        snapshot.heapHandle = m_meshState.m_frameBindings.meshView.heapHandle;
    return snapshot;
}

bool RendererMeshSystem::snapshotAcceptedMeshViewWorldToClip(Float44& outWorldToClip)const noexcept{
    if(!m_meshState.m_meshViewGpuDataValid)
        return false;

    ECSRenderDetail::MeshViewGpuData acceptedView;
    NWB_MEMCPY(&acceptedView, sizeof(acceptedView), m_meshState.m_meshViewGpuData, sizeof(m_meshState.m_meshViewGpuData));
    outWorldToClip = acceptedView.worldToClip;
    return true;
}

bool RendererMeshSystem::prepareMeshViewBufferUpload(
    const f32 fallbackAspectRatio,
    ECSRenderDetail::MeshViewGpuData& outViewState,
    bool& outUploadRequired
)const{
    NWB_ASSERT(m_meshState.m_meshViewBuffer);

    outViewState = ECSRenderDetail::ResolveMeshViewState(m_world, fallbackAspectRatio);
    outUploadRequired = !(
        m_meshState.m_meshViewGpuDataValid
        && NWB_MEMCMP(m_meshState.m_meshViewGpuData, &outViewState, sizeof(outViewState)) == 0
    );
    return true;
}

void RendererMeshSystem::confirmMeshViewBufferUpload(const ECSRenderDetail::MeshViewGpuData& viewState){
    NWB_MEMCPY(m_meshState.m_meshViewGpuData, sizeof(m_meshState.m_meshViewGpuData), &viewState, sizeof(viewState));
    m_meshState.m_meshViewGpuDataValid = true;
}

void RendererMeshSystem::invalidateMeshViewBufferUploadMirror(){
    m_meshState.m_meshViewGpuDataValid = false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

