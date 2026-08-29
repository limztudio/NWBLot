// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/material/renderer_draw_types.h>
#include <impl/ecs_render/mesh/mesh_system.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AvboitAliasFreeComputeEmulationGraphPlan{
    using DrawItemVector = Vector<MaterialPassDrawItem, Core::Alloc::GlobalArena>;
    using BufferVector = Vector<Core::BufferHandle, Core::Alloc::GlobalArena>;
    using HeapSlotVector = Vector<u32, Core::Alloc::GlobalArena>;

    DrawItemVector drawItems;
    BufferVector outputBuffers;
    HeapSlotVector outputHeapSlots;
    bool captured = false;

    explicit AvboitAliasFreeComputeEmulationGraphPlan(Core::Alloc::GlobalArena& arena)
        : drawItems(arena)
        , outputBuffers(arena)
        , outputHeapSlots(arena)
    {}

    void reset(){
        drawItems.clear();
        outputBuffers.clear();
        outputHeapSlots.clear();
        captured = false;
    }

    [[nodiscard]] bool capture(
        RendererMeshSystem& meshSystem,
        const MaterialPassDrawItems& sourceDrawItems
    ){
        reset();
        if(sourceDrawItems.computeDrawItems.empty())
            return false;

        drawItems.reserve(sourceDrawItems.computeDrawItems.size());
        outputBuffers.reserve(sourceDrawItems.computeDrawItems.size());
        outputHeapSlots.reserve(sourceDrawItems.computeDrawItems.size());
        for(const MaterialPassDrawItem& drawItem : sourceDrawItems.computeDrawItems){
            if(drawItem.pipelineKey.csgMode != MaterialPipelineCsgMode::None){
                reset();
                return false;
            }
            MeshResources* mesh = nullptr;
            if(
                !meshSystem.findMeshResources(drawItem.meshKey, mesh)
                || !mesh
                || !mesh->emulationVertexBuffer
                || !mesh->emulationVertexHeapHandle.valid()
            ){
                reset();
                return false;
            }
            for(usize outputIndex = 0u; outputIndex < outputBuffers.size(); ++outputIndex){
                if(
                    outputBuffers[outputIndex].get() == mesh->emulationVertexBuffer.get()
                    || outputHeapSlots[outputIndex] == mesh->emulationVertexHeapHandle.slot()
                ){
                    reset();
                    return false;
                }
            }
            drawItems.push_back(drawItem);
            outputBuffers.push_back(mesh->emulationVertexBuffer);
            outputHeapSlots.push_back(mesh->emulationVertexHeapHandle.slot());
        }
        captured = drawItems.size() == outputBuffers.size()
            && outputBuffers.size() == outputHeapSlots.size()
            && !drawItems.empty()
        ;
        return captured;
    }

    [[nodiscard]] bool matches(RendererMeshSystem& meshSystem)const{
        if(
            !captured
            || outputBuffers.size() != drawItems.size()
            || outputHeapSlots.size() != drawItems.size()
        )
            return false;
        for(usize drawIndex = 0u; drawIndex < drawItems.size(); ++drawIndex){
            MeshResources* mesh = nullptr;
            if(
                !meshSystem.findMeshResources(drawItems[drawIndex].meshKey, mesh)
                || !mesh
                || !mesh->emulationVertexBuffer
                || !mesh->emulationVertexHeapHandle.valid()
                || mesh->emulationVertexBuffer.get() != outputBuffers[drawIndex].get()
                || mesh->emulationVertexHeapHandle.slot() != outputHeapSlots[drawIndex]
            )
                return false;
        }
        return true;
    }

    void materialize(MaterialPassDrawItems& outDrawItems)const{
        outDrawItems.computeDrawItems.assign(drawItems.begin(), drawItems.end());
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

