// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/material/renderer_draw_types.h>
#include <impl/ecs_render/mesh/mesh_system.h>
#include <impl/ecs_render/shared/task_graph_stage.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct RegularSharedComputeEmulationGraphPlan{
    MaterialPassDrawItem drawItems[s_SharedComputeEmulationMaximumDrawCount] = {};
    Core::BufferHandle outputBuffer;
    u32 outputHeapSlot = 0u;
    usize drawCount = 0u;
    bool captured = false;

    void reset(){
        for(MaterialPassDrawItem& drawItem : drawItems)
            drawItem = {};
        outputBuffer = nullptr;
        outputHeapSlot = 0u;
        drawCount = 0u;
        captured = false;
    }

    [[nodiscard]] bool capture(
        RendererMeshSystem& meshSystem,
        const MaterialPassDrawItems& sourceDrawItems,
        const usize allowedMaxDrawCount
    ){
        reset();
        if(
            !IsSupportedSharedComputeEmulationDrawCount(allowedMaxDrawCount)
            || sourceDrawItems.computeDrawItems.size() < s_SharedComputeEmulationMinimumDrawCount
            || sourceDrawItems.computeDrawItems.size() > allowedMaxDrawCount
        )
            return false;

        drawCount = sourceDrawItems.computeDrawItems.size();
        for(usize drawIndex = 0u; drawIndex < drawCount; ++drawIndex){
            const MaterialPassDrawItem& drawItem = sourceDrawItems.computeDrawItems[drawIndex];
            if(drawItem.pipelineKey.csgMode != MaterialPipelineCsgMode::None)
                return false;

            MeshResources* mesh = nullptr;
            if(
                !meshSystem.findMeshResources(drawItem.meshKey, mesh)
                || !mesh
                || !mesh->emulationVertexBuffer
                || !mesh->emulationVertexHeapHandle.valid()
            )
                return false;

            if(drawIndex == 0u){
                outputBuffer = mesh->emulationVertexBuffer;
                outputHeapSlot = mesh->emulationVertexHeapHandle.slot();
            }
            else if(
                mesh->emulationVertexBuffer.get() != outputBuffer.get()
                || mesh->emulationVertexHeapHandle.slot() != outputHeapSlot
            )
                return false;

            drawItems[drawIndex] = drawItem;
        }
        captured = static_cast<bool>(outputBuffer);
        return captured;
    }

    [[nodiscard]] bool matches(RendererMeshSystem& meshSystem, const usize drawIndex)const{
        if(!captured || drawIndex >= drawCount || !outputBuffer)
            return false;

        MeshResources* mesh = nullptr;
        return meshSystem.findMeshResources(drawItems[drawIndex].meshKey, mesh)
            && mesh
            && mesh->emulationVertexBuffer
            && mesh->emulationVertexHeapHandle.valid()
            && mesh->emulationVertexBuffer.get() == outputBuffer.get()
            && mesh->emulationVertexHeapHandle.slot() == outputHeapSlot
        ;
    }

    void materialize(const usize drawIndex, MaterialPassDrawItems& outDrawItems)const{
        NWB_ASSERT(captured && drawIndex < drawCount);
        outDrawItems.computeDrawItems.push_back(drawItems[drawIndex]);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

