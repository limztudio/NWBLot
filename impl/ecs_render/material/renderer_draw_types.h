// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/material/renderer_pipeline_types.h>

#include <impl/assets/graphics/mesh/binding_slots.h>
#include <impl/assets/graphics/mesh/runtime_constants.h>
#include <impl/ecs_mesh/runtime/mesh.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MaterialPassMeshResourceSnapshot{
    RuntimeMeshBuffers sourceBuffers;
    Core::GpuDescriptorHandle geometryHeapHandles[NWB_MESH_INSTANCE_GEOMETRY_SLOT_COUNT] = {};
    Core::BufferHandle emulationVertexBuffer;
    Core::GpuDescriptorHandle emulationVertexHeapHandle = Core::GpuDescriptorHandle::invalid();
    u32 meshletCount = 0u;
    u32 meshletPrimitiveIndexCount = 0u;
    bool runtimeMesh = false;
    bool dynamicMeshletBoundsFresh = false;
    bool dynamicMeshletConesFresh = false;

    [[nodiscard]] bool geometryHeapHandlesReady()const noexcept{
        constexpr u32 s_SourceBindingSlots[] = {
            NWB_MESH_BINDING_POSITION,
            NWB_MESH_BINDING_NORMAL,
            NWB_MESH_BINDING_TANGENT,
            NWB_MESH_BINDING_UV0,
            NWB_MESH_BINDING_COLOR,
            NWB_MESH_BINDING_MESHLET_DESC,
            NWB_MESH_BINDING_MESHLET_BOUNDS,
            NWB_MESH_BINDING_MESHLET_POSITION_REFS,
            NWB_MESH_BINDING_MESHLET_ATTRIBUTE_REFS,
            NWB_MESH_BINDING_MESHLET_LOCAL_VERTEX_REFS,
            NWB_MESH_BINDING_MESHLET_PRIMITIVE_INDICES,
        };
        for(const u32 bindingSlot : s_SourceBindingSlots){
            const Core::GpuDescriptorHandle handle = geometryHeapHandles[bindingSlot];
            if(
                !handle.valid()
                || handle.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer
            )
                return false;
        }
        return true;
    }

    [[nodiscard]] bool valid()const noexcept{
        return
            sourceBuffers.buffersValid()
            && geometryHeapHandlesReady()
            && meshletCount > 0u
            && meshletPrimitiveIndexCount > 0u
        ;
    }
};

template<typename BufferVector>
[[nodiscard]] inline bool MaterialPassEmulationOutputBufferCaptured(
    const BufferVector& outputBuffers,
    const Core::BufferHandle& buffer
)noexcept{
    for(const Core::BufferHandle& existing : outputBuffers){
        if(existing.get() == buffer.get())
            return true;
    }
    return false;
}

template<typename BufferVector, typename SlotVector>
[[nodiscard]] inline bool MaterialPassEmulationOutputCaptured(
    const BufferVector& outputBuffers,
    const SlotVector& outputHeapSlots,
    const Core::BufferHandle& buffer,
    const u32 heapSlot
)noexcept{
    for(usize outputIndex = 0u; outputIndex < outputBuffers.size(); ++outputIndex){
        if(
            outputBuffers[outputIndex].get() == buffer.get()
            || outputHeapSlots[outputIndex] == heapSlot
        )
            return true;
    }
    return false;
}

struct MaterialPassPipelineResourceSnapshot{
    Core::GraphicsPipelineHandle emulationPipeline;
    Core::MeshletPipelineHandle meshletPipeline;
    Core::ComputePipelineHandle computePipeline;
};

template<typename BufferHandler>
void ForEachMaterialPassMeshSourceBuffer(
    const MaterialPassMeshResourceSnapshot& mesh,
    BufferHandler&& handler
){
    handler(mesh.sourceBuffers.positionBuffer);
    handler(mesh.sourceBuffers.normalBuffer);
    handler(mesh.sourceBuffers.tangentBuffer);
    handler(mesh.sourceBuffers.uv0Buffer);
    handler(mesh.sourceBuffers.colorBuffer);
    handler(mesh.sourceBuffers.meshletDescBuffer);
    handler(mesh.sourceBuffers.meshletBoundsBuffer);
    handler(mesh.sourceBuffers.meshletPositionRefDeltaBuffer);
    handler(mesh.sourceBuffers.meshletAttributeRefDeltaBuffer);
    handler(mesh.sourceBuffers.meshletLocalVertexRefBuffer);
    handler(mesh.sourceBuffers.meshletPrimitiveIndexBuffer);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MaterialPassDrawItem{
    Name meshKey = NAME_NONE;
    MaterialPipelineKey pipelineKey;
    MaterialPassMeshResourceSnapshot meshResources;
    MaterialPassPipelineResourceSnapshot pipelineResources;
    u32 instanceIndex = 0;
    u32 materialConstantByteOffset = 0u;
    u32 shadingModelId = 0u;
    bool meshletConeCullScaleSafe = false;
};

struct MaterialInstanceMutableCacheEntry{
    Name materialName = NAME_NONE;
    Name materialInterface = NAME_NONE;
    u64 typedLayoutHash = 0u;
    u64 revision = 0u;
    MaterialTypedByteVector mutableTypedBytes;

    explicit MaterialInstanceMutableCacheEntry(Core::Alloc::GlobalArena& arena)
        : mutableTypedBytes(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using MaterialPassDrawItemVector = Vector<MaterialPassDrawItem, Core::Alloc::ScratchArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MaterialPassDrawItems{
    MaterialPassDrawItemVector meshDrawItems;
    MaterialPassDrawItemVector computeDrawItems;

    explicit MaterialPassDrawItems(Core::Alloc::ScratchArena& arena)
        : meshDrawItems(arena)
        , computeDrawItems(arena)
    {}

    [[nodiscard]] bool empty()const noexcept{ return meshDrawItems.empty() && computeDrawItems.empty(); }
    void reserve(const usize capacity){
        meshDrawItems.reserve(capacity);
        computeDrawItems.reserve(capacity);
    }
};

struct MaterialPassDrawItemPartitions{
    MaterialPassDrawItems regular;
    MaterialPassDrawItems csg;
    MaterialPassDrawItems csgReceiverSurface;

    explicit MaterialPassDrawItemPartitions(Core::Alloc::ScratchArena& arena)
        : regular(arena)
        , csg(arena)
        , csgReceiverSurface(arena)
    {}

    [[nodiscard]] bool empty()const noexcept{ return regular.empty() && csg.empty() && csgReceiverSurface.empty(); }
    void reserve(const usize capacity){
        regular.reserve(capacity);
        csg.reserve(capacity);
        csgReceiverSurface.reserve(capacity);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

