// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_private.h"

#include "csg_cap_builder.h"
#include "csg_cap_proxy.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererCsgSystem::appendCsgReceiverCapGeometry(
    const MeshResources& mesh,
    const Scene::TransformComponent* transform,
    const Name& receiverGroup,
    const CsgReceiverPass::Enum receiverPass,
    const u32 receiverIndex,
    const CsgReceiverRangeGpuData& receiverRange,
    CsgFrameGpuData& csgFrameData,
    CsgCapDrawItemVector& capDrawItems
)const{
    if(receiverRange.cutterCount == 0u)
        return true;

    if(receiverIndex >= csgFrameData.receiverRanges.size())
        return false;
    if(static_cast<usize>(receiverRange.firstCutter) + static_cast<usize>(receiverRange.cutterCount) > csgFrameData.cutters.size())
        return false;

    Core::Alloc::ScratchArena& scratchArena = csgFrameData.capVertices.get_allocator().arena();
    CsgCapProxyBounds receiverBounds;
    if(!ECSRenderCsgCapProxy::BuildReceiverBounds(mesh.csgLocalBounds, transform, receiverBounds))
        return false;

    for(u32 cutterOffset = 0u; cutterOffset < receiverRange.cutterCount; ++cutterOffset){
        const u32 cutterIndex = receiverRange.firstCutter + cutterOffset;
        const CsgCutterGpuData& cutter = csgFrameData.cutters[cutterIndex];
        if(!ECSRenderCsgCapProxy::AppendDrawItem(
            csgShapeRegistry(),
            receiverBounds,
            receiverGroup,
            receiverPass,
            receiverIndex,
            cutterIndex,
            cutter,
            csgFrameData
        ))
            return false;
        if(mesh.csgCapTriangles.empty())
            continue;

        CsgShapeTypeInfo shapeType;
        const CsgShapeTypeInfo* shapeTypePtr = nullptr;
        if(csgShapeRegistry().findShapeType(cutter.shapeType, shapeType))
            shapeTypePtr = &shapeType;

        const u8* parameterBytes = ECSRenderCsgCapProxy::CsgCutterParameterBytes(csgFrameData, cutter);
        if(cutter.parameterByteSize != 0u && !parameterBytes)
            return false;

        if(csgFrameData.capVertices.size() > static_cast<usize>(Limit<u32>::s_Max))
            return false;
        const u32 firstVertex = static_cast<u32>(csgFrameData.capVertices.size());
        if(!ECSRenderCsgCapBuilder::AppendCapGeometry(
            mesh.csgCapTriangles,
            transform,
            receiverIndex,
            cutter,
            shapeTypePtr,
            parameterBytes,
            static_cast<usize>(cutter.parameterByteSize),
            cutterIndex,
            csgFrameData.capVertices,
            scratchArena
        ))
            return false;

        const usize appendedVertexCount = csgFrameData.capVertices.size() - static_cast<usize>(firstVertex);
        if(appendedVertexCount == 0u)
            continue;
        if(appendedVertexCount > static_cast<usize>(Limit<u32>::s_Max))
            return false;

        capDrawItems.push_back(CsgCapDrawItem{
            firstVertex,
            static_cast<u32>(appendedVertexCount),
        });
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

