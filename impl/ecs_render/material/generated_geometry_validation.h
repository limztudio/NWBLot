// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "renderer_draw_types.h"
#include "generated_geometry_state.h"
#include "compute_emulation_output_index.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Plan, typename AcceptMode>
[[nodiscard]] bool CaptureAliasFreeGeneratedGeometryDraws(Plan& plan, const MaterialPassDrawItems& source, Core::Alloc::ScratchArena& scratchArena, AcceptMode acceptMode){
    plan.drawItems.reserve(source.computeDrawItems.size());
    plan.outputBuffers.reserve(source.computeDrawItems.size());
    plan.outputLayouts.reserve(source.computeDrawItems.size());
    plan.outputHeapSlots.reserve(source.computeDrawItems.size());
    MaterialPassEmulationOutputIndex<Core::Buffer*> outputs(scratchArena);
    MaterialPassEmulationOutputIndex<u32> slots(scratchArena);
    for(const MaterialPassDrawItem& drawItem : source.computeDrawItems){
        if(!acceptMode(drawItem.pipelineKey.csgMode))
            return false;
        const MaterialPassMeshResourceSnapshot& mesh = drawItem.meshResources;
        if(!mesh.emulationVertexBuffer || !mesh.emulationVertexHeapHandle.valid())
            return false;
        if(!outputs.insert(mesh.emulationVertexBuffer.get()) || !slots.insert(mesh.emulationVertexHeapHandle.slot()))
            return false;
        plan.drawItems.push_back(drawItem);
        plan.outputBuffers.push_back(mesh.emulationVertexBuffer);
        plan.outputLayouts.push_back({ mesh.emulationIndexByteOffset, drawItem.pipelineResources.indexedGeometryOutput });
        plan.outputHeapSlots.push_back(mesh.emulationVertexHeapHandle.slot());
    }
    return true;
}


[[nodiscard]] NWB_INLINE bool MatchesRetainedGeneratedGeometryOutputs(
    const bool captured,
    const Vector<MaterialPassDrawItem, Core::Alloc::GlobalArena>& drawItems,
    const Vector<Core::BufferHandle, Core::Alloc::GlobalArena>& outputBuffers,
    const Vector<GeneratedGeometryOutputLayout, Core::Alloc::GlobalArena>& outputLayouts,
    const Vector<u32, Core::Alloc::GlobalArena>& outputHeapSlots
){
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


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

