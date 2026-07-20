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
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    Core::BufferHandle meshViewBuffer = graphics().createBuffer(meshViewBufferDesc);
    if(!meshViewBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create mesh view buffer"));
        return false;
    }

    drawState().m_meshViewBuffer = Move(meshViewBuffer);
    destroyMeshBindingSets();
    return true;
}

bool RendererMeshSystem::updateMeshViewBuffer(Core::CommandList& commandList, const f32 fallbackAspectRatio){
    NWB_ASSERT(drawState().m_meshViewBuffer);

    const ECSRenderDetail::MeshViewGpuData viewState = ECSRenderDetail::ResolveMeshViewState(world(), fallbackAspectRatio);
    if(
        drawState().m_meshViewGpuDataValid
        && NWB_MEMCMP(drawState().m_meshViewGpuData, &viewState, sizeof(viewState)) == 0
    )
        return true;

    commandList.setBufferState(drawState().m_meshViewBuffer.get(), Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.writeBuffer(drawState().m_meshViewBuffer.get(), &viewState, sizeof(viewState));
    commandList.setBufferState(drawState().m_meshViewBuffer.get(), Core::ResourceStates::ConstantBuffer);
    commandList.commitBarriers();
    NWB_MEMCPY(drawState().m_meshViewGpuData, sizeof(drawState().m_meshViewGpuData), &viewState, sizeof(viewState));
    drawState().m_meshViewGpuDataValid = true;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

