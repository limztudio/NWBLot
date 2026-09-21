// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/csg/renderer_csg_types.h>
#include <impl/ecs_render/material/renderer_draw_types.h>
#include <impl/ecs_render/material/compute_emulation_output_index.h>
#include <impl/ecs_render/material/generated_geometry_state.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct OpaqueCsgReceiverComputeEmulationGraphPlan{
    using DrawItemVector = Vector<MaterialPassDrawItem, Core::Alloc::GlobalArena>;
    using BufferVector = Vector<Core::BufferHandle, Core::Alloc::GlobalArena>;
    using OutputLayoutVector = Vector<GeneratedGeometryOutputLayout, Core::Alloc::GlobalArena>;
    using ReceiverRangeVector = Vector<CsgReceiverRangeGpuData, Core::Alloc::GlobalArena>;
    using CutterVector = Vector<CsgCutterGpuData, Core::Alloc::GlobalArena>;

    DrawItemVector meshDrawItems;
    DrawItemVector drawItems;
    DrawItemVector regularDrawItems;
    BufferVector outputBuffers;
    OutputLayoutVector outputLayouts;
    BufferVector regularOutputBuffers;
    OutputLayoutVector regularOutputLayouts;
    ReceiverRangeVector receiverRanges;
    CutterVector cutters;
    CsgFrameWorkRegion workRegion;
    bool captured = false;

    explicit OpaqueCsgReceiverComputeEmulationGraphPlan(Core::Alloc::GlobalArena& arena)
        : meshDrawItems(arena)
        , drawItems(arena)
        , regularDrawItems(arena)
        , outputBuffers(arena)
        , outputLayouts(arena)
        , regularOutputBuffers(arena)
        , regularOutputLayouts(arena)
        , receiverRanges(arena)
        , cutters(arena)
    {}

    void reset(){
        meshDrawItems.clear();
        drawItems.clear();
        regularDrawItems.clear();
        outputBuffers.clear();
        outputLayouts.clear();
        regularOutputBuffers.clear();
        regularOutputLayouts.clear();
        receiverRanges.clear();
        cutters.clear();
        workRegion = {};
        captured = false;
    }

    [[nodiscard]] bool capture(
        const MaterialPassDrawItems& receiverSurfaceDrawItems,
        const MaterialPassDrawItems& sourceRegularDrawItems,
        const CsgFrameGpuData& csgFrameData,
        Core::Alloc::ScratchArena& scratchArena
    ){
        reset();
        if(receiverSurfaceDrawItems.computeDrawItems.empty() || !csgFrameData.hasWork())
            return false;

        meshDrawItems.reserve(receiverSurfaceDrawItems.meshDrawItems.size());
        meshDrawItems.assign(
            receiverSurfaceDrawItems.meshDrawItems.begin(),
            receiverSurfaceDrawItems.meshDrawItems.end()
        );
        regularDrawItems.reserve(sourceRegularDrawItems.computeDrawItems.size());
        regularDrawItems.assign(
            sourceRegularDrawItems.computeDrawItems.begin(),
            sourceRegularDrawItems.computeDrawItems.end()
        );
        drawItems.reserve(receiverSurfaceDrawItems.computeDrawItems.size());
        outputBuffers.reserve(receiverSurfaceDrawItems.computeDrawItems.size());
        outputLayouts.reserve(receiverSurfaceDrawItems.computeDrawItems.size());
        regularOutputBuffers.reserve(regularDrawItems.size());
        regularOutputLayouts.reserve(regularDrawItems.size());
        MaterialPassEmulationOutputIndex<Core::Buffer*> outputs(scratchArena);
        for(const MaterialPassDrawItem& regularDrawItem : regularDrawItems){
            const MaterialPassMeshResourceSnapshot& regularMesh = regularDrawItem.meshResources;
            if(
                !regularMesh.emulationVertexBuffer
                || !regularMesh.emulationVertexHeapHandle.valid()
            ){
                reset();
                return false;
            }
            regularOutputBuffers.push_back(regularMesh.emulationVertexBuffer);
            regularOutputLayouts.push_back({ regularMesh.emulationIndexByteOffset, regularDrawItem.pipelineResources.indexedGeometryOutput });
            outputs.include(regularMesh.emulationVertexBuffer.get());
        }
        for(const MaterialPassDrawItem& drawItem : receiverSurfaceDrawItems.computeDrawItems){
            if(drawItem.pipelineKey.csgMode == MaterialPipelineCsgMode::None){
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
            // Earlier ownership and receiver outputs both exclude this receiver.
            if(!outputs.insert(mesh.emulationVertexBuffer.get())){
                reset();
                return false;
            }
            drawItems.push_back(drawItem);
            outputBuffers.push_back(mesh.emulationVertexBuffer);
            outputLayouts.push_back({ mesh.emulationIndexByteOffset, drawItem.pipelineResources.indexedGeometryOutput });
        }
        receiverRanges.reserve(csgFrameData.receiverRanges.size());
        receiverRanges.assign(csgFrameData.receiverRanges.begin(), csgFrameData.receiverRanges.end());
        cutters.reserve(csgFrameData.cutters.size());
        cutters.assign(csgFrameData.cutters.begin(), csgFrameData.cutters.end());
        workRegion = csgFrameData.workRegion;
        captured = drawItems.size() == outputBuffers.size() && !drawItems.empty();
        return captured;
    }

    [[nodiscard]] bool matches(Core::Alloc::ScratchArena& scratchArena)const{
        if(
            !captured
            || outputBuffers.size() != drawItems.size()
            || outputLayouts.size() != drawItems.size()
            || regularOutputBuffers.size() != regularDrawItems.size()
            || regularOutputLayouts.size() != regularDrawItems.size()
        )
            return false;
        MaterialPassEmulationOutputIndex<Core::Buffer*> receiverOutputs(scratchArena);
        for(usize drawIndex = 0u; drawIndex < drawItems.size(); ++drawIndex){
            const MaterialPassMeshResourceSnapshot& mesh = drawItems[drawIndex].meshResources;
            if(
                !mesh.emulationVertexBuffer
                || !mesh.emulationVertexHeapHandle.valid()
                || mesh.emulationVertexBuffer.get() != outputBuffers[drawIndex].get()
                || !outputLayouts[drawIndex].matches(mesh.emulationIndexByteOffset, drawItems[drawIndex].pipelineResources.indexedGeometryOutput)
            )
                return false;
            receiverOutputs.include(mesh.emulationVertexBuffer.get());
        }
        for(usize drawIndex = 0u; drawIndex < regularDrawItems.size(); ++drawIndex){
            const MaterialPassMeshResourceSnapshot& mesh = regularDrawItems[drawIndex].meshResources;
            if(
                !mesh.emulationVertexBuffer
                || !mesh.emulationVertexHeapHandle.valid()
                || mesh.emulationVertexBuffer.get() != regularOutputBuffers[drawIndex].get()
                || !regularOutputLayouts[drawIndex].matches(mesh.emulationIndexByteOffset, regularDrawItems[drawIndex].pipelineResources.indexedGeometryOutput)
            )
                return false;
            if(receiverOutputs.contains(mesh.emulationVertexBuffer.get()))
                return false;
        }
        return true;
    }

    void materialize(MaterialPassDrawItems& outDrawItems, CsgFrameGpuData& outCsgFrameData)const{
        outDrawItems.meshDrawItems.reserve(meshDrawItems.size());
        outDrawItems.meshDrawItems.assign(meshDrawItems.begin(), meshDrawItems.end());
        outDrawItems.computeDrawItems.reserve(drawItems.size());
        outDrawItems.computeDrawItems.assign(drawItems.begin(), drawItems.end());
        outCsgFrameData.receiverRanges.reserve(receiverRanges.size());
        outCsgFrameData.receiverRanges.assign(receiverRanges.begin(), receiverRanges.end());
        outCsgFrameData.cutters.reserve(cutters.size());
        outCsgFrameData.cutters.assign(cutters.begin(), cutters.end());
        outCsgFrameData.workRegion = workRegion;
    }
};


struct OpaqueCsgIntervalSampleComputeEmulationGraphPlan{
    using DrawItemVector = Vector<MaterialPassDrawItem, Core::Alloc::GlobalArena>;
    using BufferVector = Vector<Core::BufferHandle, Core::Alloc::GlobalArena>;
    using OutputLayoutVector = Vector<GeneratedGeometryOutputLayout, Core::Alloc::GlobalArena>;
    using HeapSlotVector = Vector<u32, Core::Alloc::GlobalArena>;
    using ReceiverRangeVector = Vector<CsgReceiverRangeGpuData, Core::Alloc::GlobalArena>;
    using CutterVector = Vector<CsgCutterGpuData, Core::Alloc::GlobalArena>;

    DrawItemVector meshDrawItems;
    DrawItemVector drawItems;
    BufferVector outputBuffers;
    OutputLayoutVector outputLayouts;
    HeapSlotVector outputHeapSlots;
    ReceiverRangeVector receiverRanges;
    CutterVector cutters;
    CsgFrameWorkRegion workRegion;
    bool captured = false;

    explicit OpaqueCsgIntervalSampleComputeEmulationGraphPlan(Core::Alloc::GlobalArena& arena)
        : meshDrawItems(arena)
        , drawItems(arena)
        , outputBuffers(arena)
        , outputLayouts(arena)
        , outputHeapSlots(arena)
        , receiverRanges(arena)
        , cutters(arena)
    {}

    void reset(){
        meshDrawItems.clear();
        drawItems.clear();
        outputBuffers.clear();
        outputLayouts.clear();
        outputHeapSlots.clear();
        receiverRanges.clear();
        cutters.clear();
        workRegion = {};
        captured = false;
    }

    [[nodiscard]] bool capture(
        const MaterialPassDrawItems& sourceDrawItems,
        const CsgFrameGpuData& csgFrameData,
        Core::Alloc::ScratchArena& scratchArena
    ){
        reset();
        if(sourceDrawItems.computeDrawItems.empty() || !csgFrameData.hasWork())
            return false;

        meshDrawItems.reserve(sourceDrawItems.meshDrawItems.size());
        meshDrawItems.assign(sourceDrawItems.meshDrawItems.begin(), sourceDrawItems.meshDrawItems.end());
        drawItems.reserve(sourceDrawItems.computeDrawItems.size());
        outputBuffers.reserve(sourceDrawItems.computeDrawItems.size());
        outputLayouts.reserve(sourceDrawItems.computeDrawItems.size());
        outputHeapSlots.reserve(sourceDrawItems.computeDrawItems.size());
        MaterialPassEmulationOutputIndex<Core::Buffer*> outputs(scratchArena);
        MaterialPassEmulationOutputIndex<u32> slots(scratchArena);
        for(const MaterialPassDrawItem& drawItem : sourceDrawItems.computeDrawItems){
            if(drawItem.pipelineKey.csgMode == MaterialPipelineCsgMode::None){
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
            if(!outputs.insert(mesh.emulationVertexBuffer.get()) || !slots.insert(mesh.emulationVertexHeapHandle.slot())){
                reset();
                return false;
            }
            drawItems.push_back(drawItem);
            outputBuffers.push_back(mesh.emulationVertexBuffer);
            outputLayouts.push_back({ mesh.emulationIndexByteOffset, drawItem.pipelineResources.indexedGeometryOutput });
            outputHeapSlots.push_back(mesh.emulationVertexHeapHandle.slot());
        }
        receiverRanges.reserve(csgFrameData.receiverRanges.size());
        receiverRanges.assign(csgFrameData.receiverRanges.begin(), csgFrameData.receiverRanges.end());
        cutters.reserve(csgFrameData.cutters.size());
        cutters.assign(csgFrameData.cutters.begin(), csgFrameData.cutters.end());
        workRegion = csgFrameData.workRegion;
        captured = drawItems.size() == outputBuffers.size()
            && outputBuffers.size() == outputHeapSlots.size()
            && !drawItems.empty();
        return captured;
    }

    [[nodiscard]] bool matches()const{
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

    void materialize(MaterialPassDrawItems& outDrawItems, CsgFrameGpuData& outCsgFrameData)const{
        outDrawItems.meshDrawItems.reserve(meshDrawItems.size());
        outDrawItems.meshDrawItems.assign(meshDrawItems.begin(), meshDrawItems.end());
        outDrawItems.computeDrawItems.reserve(drawItems.size());
        outDrawItems.computeDrawItems.assign(drawItems.begin(), drawItems.end());
        outCsgFrameData.receiverRanges.reserve(receiverRanges.size());
        outCsgFrameData.receiverRanges.assign(receiverRanges.begin(), receiverRanges.end());
        outCsgFrameData.cutters.reserve(cutters.size());
        outCsgFrameData.cutters.assign(cutters.begin(), cutters.end());
        outCsgFrameData.workRegion = workRegion;
    }
};


// Transparent CSG exposes surface->span before combine; occupancy uploads depend on Combine.


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

