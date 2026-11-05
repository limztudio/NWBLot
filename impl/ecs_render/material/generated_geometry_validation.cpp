// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "generated_geometry_validation.h"

#include "task_graph_compute_emulation_plan.h"
#include "task_graph_opaque_compute_emulation_plan.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 MatchesRetainedGeneratedGeometryOutputs(
    const b8 captured,
    const Span<const MaterialPassDrawItem> drawItems,
    const Span<const Core::BufferHandle> outputBuffers,
    const Span<const GeneratedGeometryOutputLayout> outputLayouts,
    const Span<const u32> outputHeapSlots
)noexcept{
    if(!captured || outputBuffers.size() != drawItems.size() || outputLayouts.size() != drawItems.size() || outputHeapSlots.size() != drawItems.size())
        return false;
    for(usize drawIndex = 0u; drawIndex < drawItems.size(); ++drawIndex){
        const MaterialPassMeshResourceSnapshot& mesh = drawItems[drawIndex].meshResources;
        if(
            !mesh.emulationVertexBuffer
            || !mesh.emulationVertexHeapHandle.valid()
            || mesh.emulationVertexBuffer.get() != outputBuffers[drawIndex].get()
            || !outputLayouts[drawIndex].matches(mesh.emulationIndexByteOffset, drawItems[drawIndex].pipelineResources.indexedGeometryOutput)
            || mesh.emulationVertexHeapHandle.slot() != outputHeapSlots[drawIndex]
        )
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void AliasFreeComputeEmulationGraphPlan::reset()noexcept{
    drawItems.clear();
    outputBuffers.clear();
    outputLayouts.clear();
    outputHeapSlots.clear();
    captured = false;
}

b8 AliasFreeComputeEmulationGraphPlan::capture(const MaterialPassDrawItemsView& sourceDrawItems, Core::Alloc::ScratchArena& scratchArena){
    reset();
    if(sourceDrawItems.computeDrawItems.empty())
        return false;

    if(!CaptureAliasFreeGeneratedGeometryDraws(sourceDrawItems, [](const MaterialPipelineCsgMode::Enum mode)noexcept{ return mode == MaterialPipelineCsgMode::None; }, *this, scratchArena)){
        reset();
        return false;
    }
    captured = drawItems.size() == outputBuffers.size() && outputBuffers.size() == outputHeapSlots.size() && !drawItems.empty();
    return captured;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void OpaqueRegularComputeEmulationGraphPlan::reset()noexcept{
    meshDrawItems.clear();
    indexedDrawItems.clear();
    AliasFreeComputeEmulationGraphPlan::reset();
}

b8 OpaqueRegularComputeEmulationGraphPlan::capture(const MaterialPassDrawItemsView& sourceDrawItems, Core::Alloc::ScratchArena& scratchArena){
    reset();
    if(sourceDrawItems.computeDrawItems.empty())
        return false;

    meshDrawItems.reserve(sourceDrawItems.meshDrawItems.size());
    meshDrawItems.assign(sourceDrawItems.meshDrawItems.begin(), sourceDrawItems.meshDrawItems.end());
    indexedDrawItems.reserve(sourceDrawItems.indexedDrawItems.size());
    indexedDrawItems.assign(sourceDrawItems.indexedDrawItems.begin(), sourceDrawItems.indexedDrawItems.end());
    if(!AliasFreeComputeEmulationGraphPlan::capture(sourceDrawItems, scratchArena)){
        reset();
        return false;
    }
    return captured;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RegularSharedComputeEmulationGraphPlan::reset()noexcept{
    for(MaterialPassDrawItem& drawItem : drawItems)
        drawItem = {};
    for(GeneratedGeometryEquivalenceKey& key : equivalenceKeys)
        key = {};
    outputBuffer = nullptr;
    outputHeapSlot = 0u;
    drawCount = 0u;
    captured = false;
}

b8 RegularSharedComputeEmulationGraphPlan::capture(const MaterialPassDrawItemsView& sourceDrawItems, const usize allowedMaxDrawCount)noexcept{
    reset();
    if(!IsSupportedSharedComputeEmulationDrawCount(allowedMaxDrawCount) || sourceDrawItems.computeDrawItems.size() < s_SharedComputeEmulationMinimumDrawCount || sourceDrawItems.computeDrawItems.size() > allowedMaxDrawCount)
        return false;

    drawCount = sourceDrawItems.computeDrawItems.size();
    for(usize drawIndex = 0u; drawIndex < drawCount; ++drawIndex){
        const MaterialPassDrawItem& drawItem = sourceDrawItems.computeDrawItems[drawIndex];
        if(drawItem.pipelineKey.csgMode != MaterialPipelineCsgMode::None)
            return false;

        const MaterialPassMeshResourceSnapshot& mesh = drawItem.meshResources;
        if(!mesh.emulationVertexBuffer || !mesh.emulationVertexHeapHandle.valid())
            return false;

        if(drawIndex == 0u){
            outputBuffer = mesh.emulationVertexBuffer;
            outputHeapSlot = mesh.emulationVertexHeapHandle.slot();
        }
        else if(mesh.emulationVertexBuffer.get() != outputBuffer.get() || mesh.emulationVertexHeapHandle.slot() != outputHeapSlot)
            return false;

        drawItems[drawIndex] = drawItem;
        equivalenceKeys[drawIndex] = MakeGeneratedGeometryEquivalenceKey(
            drawItem.meshKey,
            drawItem.pipelineKey.material,
            drawItem.pipelineKey.pass,
            drawItem.instanceIndex,
            drawItem.materialConstantByteOffset,
            drawItem.shadingModelId,
            mesh.emulationVertexHeapHandle.valid(),
            mesh.emulationVertexHeapHandle.slot(),
            mesh.emulationIndexByteOffset,
            drawItem.pipelineResources.indexedGeometryOutput
        );
    }
    captured = static_cast<b8>(outputBuffer);
    return captured;
}

b8 RegularSharedComputeEmulationGraphPlan::matches(const usize drawIndex)const noexcept{
    if(!captured || drawIndex >= drawCount || !outputBuffer)
        return false;

    const MaterialPassDrawItem& drawItem = drawItems[drawIndex];
    const MaterialPassMeshResourceSnapshot& mesh = drawItem.meshResources;
    return
        mesh.emulationVertexBuffer
        && mesh.emulationVertexHeapHandle.valid()
        && mesh.emulationVertexBuffer.get() == outputBuffer.get()
        && mesh.emulationVertexHeapHandle.slot() == outputHeapSlot
        && equivalenceKeys[drawIndex].matches(
            drawItem.meshKey,
            drawItem.pipelineKey.material,
            drawItem.pipelineKey.pass,
            drawItem.pipelineKey.csgMode,
            drawItem.instanceIndex,
            drawItem.materialConstantByteOffset,
            drawItem.shadingModelId,
            mesh.emulationVertexHeapHandle.valid(),
            mesh.emulationVertexHeapHandle.slot(),
            mesh.emulationIndexByteOffset,
            drawItem.pipelineResources.indexedGeometryOutput
        )
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

