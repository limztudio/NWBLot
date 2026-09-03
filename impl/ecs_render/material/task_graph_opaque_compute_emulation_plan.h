// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/material/renderer_draw_types.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct OpaqueRegularComputeEmulationGraphPlan{
    using DrawItemVector = Vector<MaterialPassDrawItem, Core::Alloc::GlobalArena>;
    using BufferVector = Vector<Core::BufferHandle, Core::Alloc::GlobalArena>;

    DrawItemVector meshDrawItems;
    DrawItemVector drawItems;
    BufferVector outputBuffers;
    bool captured = false;

    explicit OpaqueRegularComputeEmulationGraphPlan(Core::Alloc::GlobalArena& arena)
        : meshDrawItems(arena)
        , drawItems(arena)
        , outputBuffers(arena)
    {}

    void reset(){
        meshDrawItems.clear();
        drawItems.clear();
        outputBuffers.clear();
        captured = false;
    }

    [[nodiscard]] bool capture(const MaterialPassDrawItems& sourceDrawItems){
        reset();
        if(sourceDrawItems.computeDrawItems.empty())
            return false;

        meshDrawItems.assign(sourceDrawItems.meshDrawItems.begin(), sourceDrawItems.meshDrawItems.end());
        drawItems.reserve(sourceDrawItems.computeDrawItems.size());
        outputBuffers.reserve(sourceDrawItems.computeDrawItems.size());
        for(const MaterialPassDrawItem& drawItem : sourceDrawItems.computeDrawItems){
            // This first split is deliberately regular opaque-only. A CSG binding may need clip/image state and
            // maintains a different producer/raster ordering contract, so it remains on the combined callback.
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
            // The original callback interleaves dispatch and raster specifically because a second instance can
            // overwrite this whole persistent buffer.  This first graph-owned slice deliberately declines that
            // case rather than moving either draw across a potentially aliasing producer.
            if(MaterialPassEmulationOutputBufferCaptured(outputBuffers, mesh.emulationVertexBuffer)){
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
        outDrawItems.meshDrawItems.assign(meshDrawItems.begin(), meshDrawItems.end());
        outDrawItems.computeDrawItems.assign(drawItems.begin(), drawItems.end());
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

