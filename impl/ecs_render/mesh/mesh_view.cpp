// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/kernel/renderer_private.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererMeshSystem::createMeshViewBuffer(){
    if(drawState().m_meshViewBuffer)
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
    Core::BufferHandle meshViewBuffer = graphics().createBuffer(meshViewBufferDesc);
    if(!meshViewBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create mesh view buffer"));
        return false;
    }

    releaseMeshFrameHeapHandles();
    drawState().m_meshViewBuffer = Move(meshViewBuffer);
    return true;
}

bool RendererMeshSystem::prepareMeshViewBufferUpload(
    const f32 fallbackAspectRatio,
    ECSRenderDetail::MeshViewGpuData& outViewState,
    bool& outUploadRequired
)const{
    NWB_ASSERT(drawState().m_meshViewBuffer);

    outViewState = ECSRenderDetail::ResolveMeshViewState(world(), fallbackAspectRatio);
    outUploadRequired = !(
        drawState().m_meshViewGpuDataValid
        && NWB_MEMCMP(drawState().m_meshViewGpuData, &outViewState, sizeof(outViewState)) == 0
    );
    return true;
}

void RendererMeshSystem::confirmMeshViewBufferUpload(const ECSRenderDetail::MeshViewGpuData& viewState){
    NWB_MEMCPY(drawState().m_meshViewGpuData, sizeof(drawState().m_meshViewGpuData), &viewState, sizeof(viewState));
    drawState().m_meshViewGpuDataValid = true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

