// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/material/renderer_draw_types.h>
#include <impl/ecs_render/shared/task_graph_stage.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Conservative geometry-equivalence key for generated-geometry reuse. A missed reuse costs performance; an
// incorrect hit corrupts rendering, so equality starts strict: same mesh, same pipeline program and pass, same
// instance payload, and same source-buffer/output-buffer identity. Deformation revision, view-specific culling, and
// CSG classification extend this key before any cross-phase producer shares them.
struct GeneratedGeometryEquivalenceKey{
    Name meshKey = NAME_NONE;
    Name material = NAME_NONE;
    MaterialPipelinePass::Enum pass = MaterialPipelinePass::Opaque;
    u32 instanceIndex = 0u;
    u32 materialConstantByteOffset = 0u;
    u32 shadingModelId = 0u;
    const Core::Buffer* outputBuffer = nullptr;
    u32 outputHeapSlot = 0u;

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
    return key;
}

// P2-B producer contract: frozen generation inputs separated from raster-pass inputs. The producer must not rely
// on mutable recording-time renderer state absent from this payload.
struct GeneratedGeometryProducerDescriptor{
    GeneratedGeometryEquivalenceKey key{};
    usize drawIndex = 0u;
    bool csgFallback = false;
    bool viewDependentCulling = true;
};

struct RegularSharedComputeEmulationGraphPlan{
    MaterialPassDrawItem drawItems[s_SharedComputeEmulationMaximumDrawCount] = {};
    GeneratedGeometryEquivalenceKey equivalenceKeys[s_SharedComputeEmulationMaximumDrawCount] = {};
    Core::BufferHandle outputBuffer;
    usize drawCount = 0u;
    u32 outputHeapSlot = 0u;
    bool captured = false;
    usize reuseOpportunities = 0u;
    usize reuseHits = 0u;
    usize rejectionAliasedOutput = 0u;
    usize rejectionCsg = 0u;

    void reset(){
        for(MaterialPassDrawItem& drawItem : drawItems)
            drawItem = {};
        for(GeneratedGeometryEquivalenceKey& key : equivalenceKeys)
            key = {};
        outputBuffer = nullptr;
        outputHeapSlot = 0u;
        drawCount = 0u;
        captured = false;
        reuseOpportunities = 0u;
        reuseHits = 0u;
        rejectionAliasedOutput = 0u;
        rejectionCsg = 0u;
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
            if(drawItem.pipelineKey.csgMode != MaterialPipelineCsgMode::None){
                ++rejectionCsg;
                return false;
            }

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
            // P2-G diagnostic: draws sharing one output buffer plus identical keys are reuse hits; draws that
            // alias the output with differing keys are counted as rejected aliasing, never silently shared.
            if(drawIndex > 0u){
                ++reuseOpportunities;
                if(equivalenceKeys[drawIndex].matches(drawItems[0u]) && equivalenceKeys[0u].matches(drawItem))
                    ++reuseHits;
                else
                    ++rejectionAliasedOutput;
            }
        }
        captured = static_cast<bool>(outputBuffer);
        return captured;
    }

    [[nodiscard]] bool matches(const usize drawIndex)const{
        if(!captured || drawIndex >= drawCount || !outputBuffer)
            return false;

        const MaterialPassDrawItem& drawItem = drawItems[drawIndex];
        const MaterialPassMeshResourceSnapshot& mesh = drawItem.meshResources;
        // P2-D lease guard: buffer identity plus the frozen equivalence key must both hold at record time.
        // A recycled buffer address with different generation inputs must never validate a stale lease.
        return mesh.emulationVertexBuffer
            && mesh.emulationVertexHeapHandle.valid()
            && mesh.emulationVertexBuffer.get() == outputBuffer.get()
            && mesh.emulationVertexHeapHandle.slot() == outputHeapSlot
            && equivalenceKeys[drawIndex].matches(drawItem)
        ;
    }

    [[nodiscard]] GeneratedGeometryProducerDescriptor producerDescriptor(const usize drawIndex)const{
        GeneratedGeometryProducerDescriptor descriptor{};
        NWB_ASSERT(captured && drawIndex < drawCount);
        descriptor.key = equivalenceKeys[drawIndex];
        descriptor.drawIndex = drawIndex;
        descriptor.csgFallback = drawItems[drawIndex].pipelineKey.csgMode != MaterialPipelineCsgMode::None;
        descriptor.viewDependentCulling = true;
        return descriptor;
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

