// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/material/renderer_draw_types.h>
#include <impl/ecs_render/material/task_graph_object_geometry_key.h>
#include <impl/ecs_render/shared/task_graph_stage.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Retain the selected draw metadata so recording rejects changed packet inputs before generating into the shared output.
struct GeneratedGeometryEquivalenceKey{
    Name meshKey = NAME_NONE;
    Name material = NAME_NONE;
    MaterialPipelinePass::Enum pass = MaterialPipelinePass::Opaque;
    u32 instanceIndex = 0u;
    u32 materialConstantByteOffset = 0u;
    u32 shadingModelId = 0u;
    const Core::Buffer* outputBuffer = nullptr;
    u32 outputHeapSlot = 0u;
    u32 emulationIndexByteOffset = 0u;
    bool indexedGeometryOutput = false;
    ObjectGeometryEquivalenceKey objectGeometry;

    [[nodiscard]] bool matches(const MaterialPassDrawItem& drawItem)const noexcept{
        return drawItem.meshKey == meshKey
            && drawItem.pipelineKey.material == material
            && drawItem.pipelineKey.pass == pass
            && drawItem.pipelineKey.csgMode == MaterialPipelineCsgMode::None
            && drawItem.instanceIndex == instanceIndex
            && drawItem.materialConstantByteOffset == materialConstantByteOffset
            && drawItem.shadingModelId == shadingModelId
            && drawItem.meshResources.emulationVertexBuffer.get() == outputBuffer
            && drawItem.meshResources.emulationVertexHeapHandle.valid()
            && drawItem.meshResources.emulationVertexHeapHandle.slot() == outputHeapSlot
            && drawItem.meshResources.emulationIndexByteOffset == emulationIndexByteOffset
            && drawItem.pipelineResources.indexedGeometryOutput == indexedGeometryOutput
            && objectGeometry.matches(drawItem)
        ;
    }
};

[[nodiscard]] inline GeneratedGeometryEquivalenceKey MakeGeneratedGeometryEquivalenceKey(const MaterialPassDrawItem& drawItem)noexcept{
    GeneratedGeometryEquivalenceKey key{};
    key.meshKey = drawItem.meshKey;
    key.material = drawItem.pipelineKey.material;
    key.pass = drawItem.pipelineKey.pass;
    key.instanceIndex = drawItem.instanceIndex;
    key.materialConstantByteOffset = drawItem.materialConstantByteOffset;
    key.shadingModelId = drawItem.shadingModelId;
    key.outputBuffer = drawItem.meshResources.emulationVertexBuffer.get();
    key.outputHeapSlot = drawItem.meshResources.emulationVertexHeapHandle.valid()
        ? drawItem.meshResources.emulationVertexHeapHandle.slot()
        : 0u
    ;
    key.emulationIndexByteOffset = drawItem.meshResources.emulationIndexByteOffset;
    key.indexedGeometryOutput = drawItem.pipelineResources.indexedGeometryOutput;
    key.objectGeometry = MakeObjectGeometryEquivalenceKey(drawItem);
    return key;
}

struct RegularSharedComputeEmulationGraphPlan{
    MaterialPassDrawItem drawItems[s_SharedComputeEmulationMaximumDrawCount] = {};
    GeneratedGeometryEquivalenceKey equivalenceKeys[s_SharedComputeEmulationMaximumDrawCount] = {};
    Core::BufferHandle outputBuffer;
    usize drawCount = 0u;
    u32 outputHeapSlot = 0u;
    bool captured = false;

    void reset(){
        for(MaterialPassDrawItem& drawItem : drawItems)
            drawItem = {};
        for(GeneratedGeometryEquivalenceKey& key : equivalenceKeys)
            key = {};
        outputBuffer = nullptr;
        outputHeapSlot = 0u;
        drawCount = 0u;
        captured = false;
    }

    [[nodiscard]] bool capture(
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

            const MaterialPassMeshResourceSnapshot& mesh = drawItem.meshResources;
            if(
                !mesh.emulationVertexBuffer
                || !mesh.emulationVertexHeapHandle.valid()
            )
                return false;

            if(drawIndex == 0u){
                outputBuffer = mesh.emulationVertexBuffer;
                outputHeapSlot = mesh.emulationVertexHeapHandle.slot();
            }
            else if(
                mesh.emulationVertexBuffer.get() != outputBuffer.get()
                || mesh.emulationVertexHeapHandle.slot() != outputHeapSlot
            )
                return false;

            drawItems[drawIndex] = drawItem;
            equivalenceKeys[drawIndex] = MakeGeneratedGeometryEquivalenceKey(drawItem);
        }
        captured = static_cast<bool>(outputBuffer);
        return captured;
    }

    [[nodiscard]] bool matches(const usize drawIndex)const{
        if(!captured || drawIndex >= drawCount || !outputBuffer)
            return false;

        const MaterialPassDrawItem& drawItem = drawItems[drawIndex];
        const MaterialPassMeshResourceSnapshot& mesh = drawItem.meshResources;
        return mesh.emulationVertexBuffer
            && mesh.emulationVertexHeapHandle.valid()
            && mesh.emulationVertexBuffer.get() == outputBuffer.get()
            && mesh.emulationVertexHeapHandle.slot() == outputHeapSlot
            && equivalenceKeys[drawIndex].matches(drawItem)
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

