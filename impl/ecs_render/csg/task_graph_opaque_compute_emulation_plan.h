// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/csg/renderer_csg_types.h>
#include <impl/ecs_render/material/renderer_draw_types.h>
#include <impl/ecs_render/mesh/mesh_system.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct OpaqueCsgReceiverComputeEmulationGraphPlan{
    using DrawItemVector = Vector<MaterialPassDrawItem, Core::Alloc::GlobalArena>;
    using BufferVector = Vector<Core::BufferHandle, Core::Alloc::GlobalArena>;
    using ReceiverRangeVector = Vector<CsgReceiverRangeGpuData, Core::Alloc::GlobalArena>;
    using CutterVector = Vector<CsgCutterGpuData, Core::Alloc::GlobalArena>;

    DrawItemVector meshDrawItems;
    DrawItemVector drawItems;
    DrawItemVector regularDrawItems;
    BufferVector outputBuffers;
    BufferVector regularOutputBuffers;
    ReceiverRangeVector receiverRanges;
    CutterVector cutters;
    CsgFrameWorkRegion workRegion;
    bool captured = false;

    explicit OpaqueCsgReceiverComputeEmulationGraphPlan(Core::Alloc::GlobalArena& arena)
        : meshDrawItems(arena)
        , drawItems(arena)
        , regularDrawItems(arena)
        , outputBuffers(arena)
        , regularOutputBuffers(arena)
        , receiverRanges(arena)
        , cutters(arena)
    {}

    void reset(){
        meshDrawItems.clear();
        drawItems.clear();
        regularDrawItems.clear();
        outputBuffers.clear();
        regularOutputBuffers.clear();
        receiverRanges.clear();
        cutters.clear();
        workRegion = {};
        captured = false;
    }

    [[nodiscard]] bool capture(
        RendererMeshSystem& meshSystem,
        const MaterialPassDrawItems& receiverSurfaceDrawItems,
        const MaterialPassDrawItems& sourceRegularDrawItems,
        const CsgFrameGpuData& csgFrameData
    ){
        reset();
        if(receiverSurfaceDrawItems.computeDrawItems.empty() || !csgFrameData.hasWork())
            return false;

        meshDrawItems.assign(
            receiverSurfaceDrawItems.meshDrawItems.begin(),
            receiverSurfaceDrawItems.meshDrawItems.end()
        );
        regularDrawItems.assign(
            sourceRegularDrawItems.computeDrawItems.begin(),
            sourceRegularDrawItems.computeDrawItems.end()
        );
        drawItems.reserve(receiverSurfaceDrawItems.computeDrawItems.size());
        outputBuffers.reserve(receiverSurfaceDrawItems.computeDrawItems.size());
        regularOutputBuffers.reserve(regularDrawItems.size());
        for(const MaterialPassDrawItem& regularDrawItem : regularDrawItems){
            MeshResources* regularMesh = nullptr;
            if(
                !meshSystem.findMeshResources(regularDrawItem.meshKey, regularMesh)
                || !regularMesh
                || !regularMesh->emulationVertexBuffer
                || !regularMesh->emulationVertexHeapHandle.valid()
            ){
                reset();
                return false;
            }
            regularOutputBuffers.push_back(regularMesh->emulationVertexBuffer);
        }
        for(const MaterialPassDrawItem& drawItem : receiverSurfaceDrawItems.computeDrawItems){
            if(drawItem.pipelineKey.csgMode == MaterialPipelineCsgMode::None){
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
            for(const Core::BufferHandle& existing : outputBuffers){
                if(existing.get() == mesh->emulationVertexBuffer.get()){
                    reset();
                    return false;
                }
            }
            // G-buffer renders regular opaque work after this producer but before receiver-surface rasterization.
            // A regular compute item that writes this output would replace the generated receiver vertices first.
            for(const Core::BufferHandle& regularOutput : regularOutputBuffers){
                if(regularOutput.get() == mesh->emulationVertexBuffer.get()){
                    reset();
                    return false;
                }
            }
            drawItems.push_back(drawItem);
            outputBuffers.push_back(mesh->emulationVertexBuffer);
        }
        receiverRanges.assign(csgFrameData.receiverRanges.begin(), csgFrameData.receiverRanges.end());
        cutters.assign(csgFrameData.cutters.begin(), csgFrameData.cutters.end());
        workRegion = csgFrameData.workRegion;
        captured = drawItems.size() == outputBuffers.size() && !drawItems.empty();
        return captured;
    }

    [[nodiscard]] bool matches(RendererMeshSystem& meshSystem)const{
        if(
            !captured
            || outputBuffers.size() != drawItems.size()
            || regularOutputBuffers.size() != regularDrawItems.size()
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
            )
                return false;
        }
        for(usize drawIndex = 0u; drawIndex < regularDrawItems.size(); ++drawIndex){
            MeshResources* mesh = nullptr;
            if(
                !meshSystem.findMeshResources(regularDrawItems[drawIndex].meshKey, mesh)
                || !mesh
                || !mesh->emulationVertexBuffer
                || !mesh->emulationVertexHeapHandle.valid()
                || mesh->emulationVertexBuffer.get() != regularOutputBuffers[drawIndex].get()
            )
                return false;
            for(const Core::BufferHandle& receiverOutput : outputBuffers){
                if(mesh->emulationVertexBuffer.get() == receiverOutput.get())
                    return false;
            }
        }
        return true;
    }

    void materialize(MaterialPassDrawItems& outDrawItems, CsgFrameGpuData& outCsgFrameData)const{
        outDrawItems.meshDrawItems.assign(meshDrawItems.begin(), meshDrawItems.end());
        outDrawItems.computeDrawItems.assign(drawItems.begin(), drawItems.end());
        outCsgFrameData.receiverRanges.assign(receiverRanges.begin(), receiverRanges.end());
        outCsgFrameData.cutters.assign(cutters.begin(), cutters.end());
        outCsgFrameData.workRegion = workRegion;
    }
};


struct OpaqueCsgIntervalSampleComputeEmulationGraphPlan{
    using DrawItemVector = Vector<MaterialPassDrawItem, Core::Alloc::GlobalArena>;
    using BufferVector = Vector<Core::BufferHandle, Core::Alloc::GlobalArena>;
    using HeapSlotVector = Vector<u32, Core::Alloc::GlobalArena>;
    using ReceiverRangeVector = Vector<CsgReceiverRangeGpuData, Core::Alloc::GlobalArena>;
    using CutterVector = Vector<CsgCutterGpuData, Core::Alloc::GlobalArena>;

    DrawItemVector meshDrawItems;
    DrawItemVector drawItems;
    BufferVector outputBuffers;
    HeapSlotVector outputHeapSlots;
    ReceiverRangeVector receiverRanges;
    CutterVector cutters;
    CsgFrameWorkRegion workRegion;
    bool captured = false;

    explicit OpaqueCsgIntervalSampleComputeEmulationGraphPlan(Core::Alloc::GlobalArena& arena)
        : meshDrawItems(arena)
        , drawItems(arena)
        , outputBuffers(arena)
        , outputHeapSlots(arena)
        , receiverRanges(arena)
        , cutters(arena)
    {}

    void reset(){
        meshDrawItems.clear();
        drawItems.clear();
        outputBuffers.clear();
        outputHeapSlots.clear();
        receiverRanges.clear();
        cutters.clear();
        workRegion = {};
        captured = false;
    }

    [[nodiscard]] bool capture(
        RendererMeshSystem& meshSystem,
        const MaterialPassDrawItems& sourceDrawItems,
        const CsgFrameGpuData& csgFrameData
    ){
        reset();
        if(sourceDrawItems.computeDrawItems.empty() || !csgFrameData.hasWork())
            return false;

        meshDrawItems.assign(sourceDrawItems.meshDrawItems.begin(), sourceDrawItems.meshDrawItems.end());
        drawItems.reserve(sourceDrawItems.computeDrawItems.size());
        outputBuffers.reserve(sourceDrawItems.computeDrawItems.size());
        outputHeapSlots.reserve(sourceDrawItems.computeDrawItems.size());
        for(const MaterialPassDrawItem& drawItem : sourceDrawItems.computeDrawItems){
            if(drawItem.pipelineKey.csgMode == MaterialPipelineCsgMode::None){
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
        receiverRanges.assign(csgFrameData.receiverRanges.begin(), csgFrameData.receiverRanges.end());
        cutters.assign(csgFrameData.cutters.begin(), csgFrameData.cutters.end());
        workRegion = csgFrameData.workRegion;
        captured = drawItems.size() == outputBuffers.size()
            && outputBuffers.size() == outputHeapSlots.size()
            && !drawItems.empty();
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

    void materialize(MaterialPassDrawItems& outDrawItems, CsgFrameGpuData& outCsgFrameData)const{
        outDrawItems.meshDrawItems.assign(meshDrawItems.begin(), meshDrawItems.end());
        outDrawItems.computeDrawItems.assign(drawItems.begin(), drawItems.end());
        outCsgFrameData.receiverRanges.assign(receiverRanges.begin(), receiverRanges.end());
        outCsgFrameData.cutters.assign(cutters.begin(), cutters.end());
        outCsgFrameData.workRegion = workRegion;
    }
};


// Prepared transparent CSG exposes receiver-surface -> span before the following interval-combine callback. The
// later phase-local occupancy uploads depend on Combine so they cannot overwrite its frozen CSG buffers first.


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

