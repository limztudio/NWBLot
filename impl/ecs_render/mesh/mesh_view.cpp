// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "mesh_system.h"
#include "mesh_view_private.h"

#include <impl/ecs_render/shared/renderer_state.h>

#include <core/common/log.h>
#include <core/graphics/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererMeshSystem::createMeshViewBuffer(){
    if(m_drawState.m_meshViewBuffer)
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
    m_drawState.m_meshViewBuffer = Move(meshViewBuffer);
    return true;
}

bool RendererMeshSystem::prepareMeshViewBufferUpload(
    const f32 fallbackAspectRatio,
    ECSRenderDetail::MeshViewGpuData& outViewState,
    bool& outUploadRequired
)const{
    NWB_ASSERT(m_drawState.m_meshViewBuffer);

    outViewState = ECSRenderDetail::ResolveMeshViewState(m_world, fallbackAspectRatio);
    outUploadRequired = !(
        m_drawState.m_meshViewGpuDataValid
        && NWB_MEMCMP(m_drawState.m_meshViewGpuData, &outViewState, sizeof(outViewState)) == 0
    );
    return true;
}

void RendererMeshSystem::confirmMeshViewBufferUpload(const ECSRenderDetail::MeshViewGpuData& viewState){
    NWB_MEMCPY(m_drawState.m_meshViewGpuData, sizeof(m_drawState.m_meshViewGpuData), &viewState, sizeof(viewState));
    m_drawState.m_meshViewGpuDataValid = true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

