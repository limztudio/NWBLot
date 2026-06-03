// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_private.h"

#include "csg_cap_proxy.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererCsgSystem::appendCsgReceiverCapProxies(
    const MeshResources& mesh,
    const Scene::TransformComponent* transform,
    const CsgReceiverPass::Enum receiverPass,
    const u32 receiverIndex,
    const CsgReceiverRangeGpuData& receiverRange,
    CsgFrameGpuData& csgFrameData
)const{
    if(receiverRange.cutterCount == 0u)
        return true;

    if(receiverIndex >= csgFrameData.receiverRanges.size())
        return false;
    if(static_cast<usize>(receiverRange.firstCutter) + static_cast<usize>(receiverRange.cutterCount) > csgFrameData.cutters.size())
        return false;

    CsgCapProxyBounds receiverBounds;
    if(!ECSRenderCsgCapProxy::BuildReceiverBounds(mesh.csgLocalBounds, transform, receiverBounds))
        return false;

    for(u32 cutterOffset = 0u; cutterOffset < receiverRange.cutterCount; ++cutterOffset){
        const u32 cutterIndex = receiverRange.firstCutter + cutterOffset;
        const CsgCutterGpuData& cutter = csgFrameData.cutters[cutterIndex];
        if(!ECSRenderCsgCapProxy::AppendGpuData(
            csgShapeRegistry(),
            receiverBounds,
            receiverPass,
            receiverIndex,
            cutterIndex,
            cutter,
            csgFrameData
        ))
            return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

