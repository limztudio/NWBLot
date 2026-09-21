// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/material/renderer_draw_types.h>
#include <impl/ecs_render/material/compute_emulation_output_index.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct OpaqueRegularComputeEmulationGraphPlan{
    using DrawItemVector = Vector<MaterialPassDrawItem, Core::Alloc::GlobalArena>;
    using BufferVector = Vector<Core::BufferHandle, Core::Alloc::GlobalArena>;

    DrawItemVector meshDrawItems;
    DrawItemVector indexedDrawItems;
    DrawItemVector drawItems;
    BufferVector outputBuffers;
    bool captured = false;

    explicit OpaqueRegularComputeEmulationGraphPlan(Core::Alloc::GlobalArena& arena)
        : meshDrawItems(arena)
        , indexedDrawItems(arena)
        , drawItems(arena)
        , outputBuffers(arena)
    {}

    void reset(){
        meshDrawItems.clear();
        indexedDrawItems.clear();
        drawItems.clear();
        outputBuffers.clear();
        captured = false;
    }

    [[nodiscard]] bool capture(const MaterialPassDrawItems& sourceDrawItems, Core::Alloc::ScratchArena& scratchArena){
        reset();
        if(sourceDrawItems.computeDrawItems.empty())
            return false;

        meshDrawItems.reserve(sourceDrawItems.meshDrawItems.size());
        meshDrawItems.assign(sourceDrawItems.meshDrawItems.begin(), sourceDrawItems.meshDrawItems.end());
        indexedDrawItems.reserve(sourceDrawItems.indexedDrawItems.size());
        indexedDrawItems.assign(sourceDrawItems.indexedDrawItems.begin(), sourceDrawItems.indexedDrawItems.end());
        drawItems.reserve(sourceDrawItems.computeDrawItems.size());
        outputBuffers.reserve(sourceDrawItems.computeDrawItems.size());
        MaterialPassEmulationOutputIndex<Core::Buffer*> outputs(scratchArena);
        for(const MaterialPassDrawItem& drawItem : sourceDrawItems.computeDrawItems){
            // First split is opaque-only; CSG stays on the combined callback.
            if(drawItem.pipelineKey.csgMode != MaterialPipelineCsgMode::None){
                reset();
                return false;
            }
            const MaterialPassMeshResourceSnapshot& mesh = drawItem.meshResources;
            if(
                !mesh.emulationVertexBuffer
                || !mesh.emulationVertexHeapHandle.valid()
            ){
                reset();
                return false;
            }
            // Decline aliasing producers rather than moving draws across them.
            if(!outputs.insert(mesh.emulationVertexBuffer.get())){
                reset();
                return false;
            }
            drawItems.push_back(drawItem);
            outputBuffers.push_back(mesh.emulationVertexBuffer);
        }
        captured = drawItems.size() == outputBuffers.size() && !drawItems.empty();
        return captured;
    }

    [[nodiscard]] bool matches(const MaterialPassDrawItemVector& currentDrawItems)const{
        if(
            !captured
            || currentDrawItems.size() != drawItems.size()
            || outputBuffers.size() != drawItems.size()
        )
            return false;

        for(usize drawIndex = 0u; drawIndex < currentDrawItems.size(); ++drawIndex){
            const MaterialPassDrawItem& expected = drawItems[drawIndex];
            const MaterialPassDrawItem& current = currentDrawItems[drawIndex];
            if(
                current.meshKey != expected.meshKey
                || current.instanceIndex != expected.instanceIndex
                || current.materialConstantByteOffset != expected.materialConstantByteOffset
                || current.shadingModelId != expected.shadingModelId
                || current.meshletConeCullScaleSafe != expected.meshletConeCullScaleSafe
                || current.meshResources.emulationIndexByteOffset != expected.meshResources.emulationIndexByteOffset
                || current.pipelineResources.indexedGeometryOutput != expected.pipelineResources.indexedGeometryOutput
            )
                return false;

            const MaterialPassMeshResourceSnapshot& mesh = current.meshResources;
            if(
                !mesh.emulationVertexBuffer
                || !mesh.emulationVertexHeapHandle.valid()
                || mesh.emulationVertexBuffer.get() != outputBuffers[drawIndex].get()
            )
                return false;
        }
        return true;
    }

    void materialize(MaterialPassDrawItems& outDrawItems)const{
        outDrawItems.meshDrawItems.reserve(meshDrawItems.size());
        outDrawItems.meshDrawItems.assign(meshDrawItems.begin(), meshDrawItems.end());
        outDrawItems.indexedDrawItems.reserve(indexedDrawItems.size());
        outDrawItems.indexedDrawItems.assign(indexedDrawItems.begin(), indexedDrawItems.end());
        outDrawItems.computeDrawItems.reserve(drawItems.size());
        outDrawItems.computeDrawItems.assign(drawItems.begin(), drawItems.end());
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

