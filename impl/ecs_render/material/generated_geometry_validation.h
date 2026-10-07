// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "renderer_draw_types.h"
#include "generated_geometry_state.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE bool MatchesRetainedGeneratedGeometryOutputs(
    const bool captured,
    const Vector<MaterialPassDrawItem, Core::Alloc::GlobalArena>& drawItems,
    const Vector<Core::BufferHandle, Core::Alloc::GlobalArena>& outputBuffers,
    const Vector<GeneratedGeometryOutputLayout, Core::Alloc::GlobalArena>& outputLayouts,
    const Vector<u32, Core::Alloc::GlobalArena>& outputHeapSlots){
    if(
        !captured
        || outputBuffers.size() != drawItems.size()
        || outputLayouts.size() != drawItems.size()
        || outputHeapSlots.size() != drawItems.size()
    )
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

