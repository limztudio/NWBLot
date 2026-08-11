// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "system.h"

#include "runtime_cache.h"
#include "resource_names.h"
#include "skin_payload.h"

#include <core/common/log.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/module.h>
#include <core/graphics/rhi/gpu_descriptor_heap.h>
#include <impl/assets/graphics/skinned_mesh/binding_slots.h>
#include <impl/ecs_mesh/runtime/buffer_upload.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_resources{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename PayloadT>
static Core::BufferHandle SetupStructuredBuffer(
    Core::Graphics& graphics,
    const Name& debugName,
    const PayloadT* payload,
    const usize count,
    const tchar* label
){
    if(MultiplyOverflows<usize>(count, sizeof(PayloadT))){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: {} payload byte size overflows"), label);
        return {};
    }

    return RuntimeMeshBufferUpload::SetupBuffer<PayloadT>(
        graphics,
        debugName,
        payload,
        count
    );
}

static bool RegisterStorageBuffer(
    Core::GpuDescriptorHeap& heap,
    Core::GpuDescriptorHandle& outHandle,
    const Core::DescriptorWriteItem& item
){
    outHandle = Core::GpuDescriptorHandle::invalid();
    const Core::GpuDescriptorHandle handle = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
    if(!handle.valid())
        return false;
    if(!heap.write(handle, item)){
        heap.free(handle);
        return false;
    }
    outHandle = handle;
    return true;
}

static bool RegisterUniformBuffer(
    Core::GpuDescriptorHeap& heap,
    Core::GpuDescriptorHandle& outHandle,
    Core::Buffer& buffer
){
    outHandle = Core::GpuDescriptorHandle::invalid();
    const Core::GpuDescriptorHandle handle = heap.allocate(Core::GpuDescriptorClass::UniformBuffer);
    if(!handle.valid())
        return false;
    if(!heap.write(handle, Core::DescriptorWriteItem::ConstantBuffer(0u, &buffer))){
        heap.free(handle);
        return false;
    }
    outHandle = handle;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void MeshSkinningSystem::releaseRuntimeResourceBindlessHeapHandles(RuntimeResources& resources){
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    if(heap.isInitialized()){
        const auto release = [&](Core::GpuDescriptorHandle& handle){
            if(handle.valid())
                heap.free(handle);
            handle = Core::GpuDescriptorHandle::invalid();
        };
        release(resources.bindlessHeapHandles.resourceSlots);
        release(resources.bindlessHeapHandles.restPosition);
        release(resources.bindlessHeapHandles.skinnedPosition);
        release(resources.bindlessHeapHandles.restNormal);
        release(resources.bindlessHeapHandles.skinnedNormal);
        release(resources.bindlessHeapHandles.restTangent);
        release(resources.bindlessHeapHandles.skinnedTangent);
        release(resources.bindlessHeapHandles.meshletDesc);
        release(resources.bindlessHeapHandles.positionRefDeltas);
        release(resources.bindlessHeapHandles.attributeRefDeltas);
        release(resources.bindlessHeapHandles.attributeSkins);
        release(resources.bindlessHeapHandles.skinInfluences);
        release(resources.bindlessHeapHandles.jointPalette);
        release(resources.bindlessHeapHandles.localVertexRefs);
        release(resources.bindlessHeapHandles.primitiveIndices);
        release(resources.bindlessHeapHandles.meshletBounds);
    }
    resources.bindlessHeapHandles = RuntimeBindlessHeapHandles{};
    resources.bindlessResourceSlots = RuntimeBindlessResourceSlots{};
    resources.bindlessResourceSlotsUploaded = false;
}

bool MeshSkinningSystem::createRuntimeResourceBindlessHeapHandles(MeshSkinningRuntimeInstance& instance, RuntimeResources& resources){
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: runtime mesh '{}' requires the initialized global descriptor heap"), instance.handle.value);
        return false;
    }
    const auto fail = [&](){
        releaseRuntimeResourceBindlessHeapHandles(resources);
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to register persistent compute buffers for runtime mesh '{}' in the descriptor heap"), instance.handle.value);
        return false;
    };
    const auto registerBuffer = [&](Core::GpuDescriptorHandle& outHandle, const Core::DescriptorWriteItem& item){
        return __hidden_resources::RegisterStorageBuffer(heap, outHandle, item);
    };

    if(
        !resources.bindlessResourceSlotsBuffer
        || !instance.restPositionBuffer || !instance.skinnedPositionBuffer
        || !instance.restNormalBuffer || !instance.skinnedNormalBuffer
        || !instance.restTangentBuffer || !instance.skinnedTangentBuffer
        || !instance.meshletDescBuffer || !instance.meshletPositionRefDeltaBuffer
        || !instance.meshletAttributeRefDeltaBuffer || !instance.attributeSkinBuffer
        || !instance.meshletLocalVertexRefBuffer || !instance.meshletPrimitiveIndexBuffer
        || !instance.meshletBoundsBuffer
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: runtime mesh '{}' has incomplete persistent compute buffers"), instance.handle.value);
        return false;
    }

    if(
        !registerBuffer(resources.bindlessHeapHandles.restPosition, Core::DescriptorWriteItem::StructuredBuffer_SRV(0u, instance.restPositionBuffer.get()))
        || !registerBuffer(resources.bindlessHeapHandles.skinnedPosition, Core::DescriptorWriteItem::StructuredBuffer_UAV(0u, instance.skinnedPositionBuffer.get()))
        || !registerBuffer(resources.bindlessHeapHandles.restNormal, Core::DescriptorWriteItem::StructuredBuffer_SRV(0u, instance.restNormalBuffer.get()))
        || !registerBuffer(resources.bindlessHeapHandles.skinnedNormal, Core::DescriptorWriteItem::StructuredBuffer_UAV(0u, instance.skinnedNormalBuffer.get()))
        || !registerBuffer(resources.bindlessHeapHandles.restTangent, Core::DescriptorWriteItem::StructuredBuffer_SRV(0u, instance.restTangentBuffer.get()))
        || !registerBuffer(resources.bindlessHeapHandles.skinnedTangent, Core::DescriptorWriteItem::StructuredBuffer_UAV(0u, instance.skinnedTangentBuffer.get()))
        || !registerBuffer(resources.bindlessHeapHandles.meshletDesc, Core::DescriptorWriteItem::StructuredBuffer_SRV(0u, instance.meshletDescBuffer.get()))
        || !registerBuffer(resources.bindlessHeapHandles.positionRefDeltas, Core::DescriptorWriteItem::RawBuffer_SRV(0u, instance.meshletPositionRefDeltaBuffer.get()))
        || !registerBuffer(resources.bindlessHeapHandles.attributeRefDeltas, Core::DescriptorWriteItem::RawBuffer_SRV(0u, instance.meshletAttributeRefDeltaBuffer.get()))
        || !registerBuffer(resources.bindlessHeapHandles.attributeSkins, Core::DescriptorWriteItem::StructuredBuffer_SRV(0u, instance.attributeSkinBuffer.get()))
        || !registerBuffer(resources.bindlessHeapHandles.localVertexRefs, Core::DescriptorWriteItem::StructuredBuffer_SRV(0u, instance.meshletLocalVertexRefBuffer.get()))
        || !registerBuffer(resources.bindlessHeapHandles.primitiveIndices, Core::DescriptorWriteItem::RawBuffer_SRV(0u, instance.meshletPrimitiveIndexBuffer.get()))
        || !registerBuffer(resources.bindlessHeapHandles.meshletBounds, Core::DescriptorWriteItem::RawBuffer_UAV(0u, instance.meshletBoundsBuffer.get()))
    )
        return fail();

    if(resources.skinCount != 0u){
        if(!resources.skinBuffer || !resources.jointPaletteBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: runtime mesh '{}' has incomplete persistent skinning payload buffers"), instance.handle.value);
            return fail();
        }
        if(
            !registerBuffer(resources.bindlessHeapHandles.skinInfluences, Core::DescriptorWriteItem::StructuredBuffer_SRV(0u, resources.skinBuffer.get()))
            || !registerBuffer(resources.bindlessHeapHandles.jointPalette, Core::DescriptorWriteItem::StructuredBuffer_SRV(0u, resources.jointPaletteBuffer.get()))
        )
            return fail();
    }

    if(instance.attributeBuffer){
        if(!registerBuffer(resources.bindlessHeapHandles.attributeBuffer, Core::DescriptorWriteItem::RawBuffer_UAV(0u, instance.attributeBuffer.get())))
            return fail();
    }
    if(!__hidden_resources::RegisterUniformBuffer(
        heap,
        resources.bindlessHeapHandles.resourceSlots,
        *resources.bindlessResourceSlotsBuffer.get()
    ))
        return fail();

    RuntimeBindlessResourceSlots& slots = resources.bindlessResourceSlots;
    slots.restPosition = resources.bindlessHeapHandles.restPosition.slot();
    slots.skinnedPosition = resources.bindlessHeapHandles.skinnedPosition.slot();
    slots.restNormal = resources.bindlessHeapHandles.restNormal.slot();
    slots.skinnedNormal = resources.bindlessHeapHandles.skinnedNormal.slot();
    slots.restTangent = resources.bindlessHeapHandles.restTangent.slot();
    slots.skinnedTangent = resources.bindlessHeapHandles.skinnedTangent.slot();
    slots.meshletDesc = resources.bindlessHeapHandles.meshletDesc.slot();
    slots.positionRefDeltas = resources.bindlessHeapHandles.positionRefDeltas.slot();
    slots.attributeRefDeltas = resources.bindlessHeapHandles.attributeRefDeltas.slot();
    slots.attributeSkins = resources.bindlessHeapHandles.attributeSkins.slot();
    if(resources.skinCount != 0u){
        slots.skinInfluences = resources.bindlessHeapHandles.skinInfluences.slot();
        slots.jointPalette = resources.bindlessHeapHandles.jointPalette.slot();
    }
    slots.localVertexRefs = resources.bindlessHeapHandles.localVertexRefs.slot();
    slots.primitiveIndices = resources.bindlessHeapHandles.primitiveIndices.slot();
    slots.meshletBounds = resources.bindlessHeapHandles.meshletBounds.slot();
    if(instance.attributeBuffer)
        slots.attributeBuffer = resources.bindlessHeapHandles.attributeBuffer.slot();
    return true;
}

bool MeshSkinningSystem::uploadRuntimeResourceBindlessSlots(
    Core::CommandList& commandList,
    RuntimeResources& resources,
    bool& outUploadRecorded
){
    outUploadRecorded = false;
    if(!resources.bindlessResourceSlotsBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: missing runtime bindless slot buffer"));
        return false;
    }

    if(!resources.bindlessResourceSlotsUploaded){
        commandList.setBufferState(resources.bindlessResourceSlotsBuffer.get(), Core::ResourceStates::CopyDest);
        commandList.commitBarriers();
        commandList.writeBuffer(
            resources.bindlessResourceSlotsBuffer.get(),
            &resources.bindlessResourceSlots,
            sizeof(resources.bindlessResourceSlots)
        );
        outUploadRecorded = true;
    }
    // Graph-owned selector blobs publish automatic/Common. Whether the immutable graph upload or this legacy
    // fallback supplied the bytes, the native compute continuation owns the final descriptor-visible transition.
    commandList.setBufferState(resources.bindlessResourceSlotsBuffer.get(), Core::ResourceStates::ConstantBuffer);
    commandList.commitBarriers();
    return true;
}

bool MeshSkinningSystem::ensureRuntimeResources(
    MeshSkinningRuntimeInstance& instance,
    const RuntimePayloadViews& payloadViews,
    RuntimeResources*& outResources,
    bool& outResourcesRebuilt
){
    outResources = nullptr;
    outResourcesRebuilt = false;
    NWB_ASSERT((payloadViews.skinInfluenceCount == 0u) == (payloadViews.jointPaletteCount == 0u));

    const bool hasActiveSkin = payloadViews.hasActiveSkin();
    NWB_ASSERT(!hasActiveSkin || payloadViews.skinInfluences);
    NWB_ASSERT(!hasActiveSkin || payloadViews.jointPalette);
    NWB_ASSERT(payloadViews.skinInfluenceCount <= static_cast<usize>(Limit<u32>::s_Max));
    NWB_ASSERT(payloadViews.jointPaletteCount <= static_cast<usize>(Limit<u32>::s_Max));

    auto [it, inserted] = m_runtimeResources.try_emplace(instance.handle.value);
    RuntimeResources& resources = it.value();
    const u32 positionCount = instance.meshletPositionRefCount;
    const u32 attributeCount = instance.meshletAttributeRefCount;
    const u32 meshletCount = static_cast<u32>(instance.meshlets.size());
    const u32 skinCount = static_cast<u32>(payloadViews.skinInfluenceCount);
    const u32 jointCount = static_cast<u32>(payloadViews.jointPaletteCount);
    const bool rebuild =
        inserted
        || resources.editRevision != instance.editRevision
        || resources.positionCount != positionCount
        || resources.attributeCount != attributeCount
        || resources.meshletCount != meshletCount
        || resources.skinCount != skinCount
        || resources.jointCount != jointCount
        || !resources.hasPersistentHeapDescriptors(hasActiveSkin, instance.attributeBuffer != nullptr)
        || (hasActiveSkin && (!resources.skinBuffer || !resources.jointPaletteBuffer))
        || (!hasActiveSkin && resources.usesSkinning())
    ;
    if(!rebuild){
        outResources = &resources;
        return true;
    }

    RuntimeResources rebuilt;
    rebuilt.handle = instance.handle;
    rebuilt.editRevision = instance.editRevision;
    rebuilt.positionCount = positionCount;
    rebuilt.attributeCount = attributeCount;
    rebuilt.meshletCount = meshletCount;
    rebuilt.skinCount = skinCount;
    rebuilt.jointCount = jointCount;

    const auto failRebuild = [&](){
        releaseRuntimeResourceBindlessHeapHandles(rebuilt);
        return false;
    };

    if(hasActiveSkin){
        const Name skinBufferName = DeriveRuntimeResourceName(instance.sourceName, instance.handle.value, instance.editRevision, "mesh_skinning_skin");
        const Name jointPaletteBufferName = DeriveRuntimeResourceName(instance.sourceName, instance.handle.value, instance.editRevision, "mesh_skinning_joints");
        if(!skinBufferName || !jointPaletteBufferName){
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to derive skinning buffer names for runtime mesh '{}'"), instance.handle.value);
            return failRebuild();
        }

        rebuilt.skinBuffer = __hidden_resources::SetupStructuredBuffer(
            m_graphics,
            skinBufferName,
            payloadViews.skinInfluences,
            payloadViews.skinInfluenceCount,
            NWB_TEXT("skin influence")
        );
        if(!rebuilt.skinBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to create skin buffer for runtime mesh '{}'"), instance.handle.value);
            return failRebuild();
        }

        rebuilt.jointPaletteBuffer = __hidden_resources::SetupStructuredBuffer(
            m_graphics,
            jointPaletteBufferName,
            payloadViews.jointPalette,
            payloadViews.jointPaletteCount,
            NWB_TEXT("joint palette")
        );
        if(!rebuilt.jointPaletteBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to create joint palette buffer for runtime mesh '{}'"), instance.handle.value);
            return failRebuild();
        }
    }

    const Name bindlessSlotsBufferName = DeriveRuntimeResourceName(
        instance.sourceName,
        instance.handle.value,
        instance.editRevision,
        "mesh_skinning_bindless_slots"
    );
    if(!bindlessSlotsBufferName){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to derive bindless slot buffer name for runtime mesh '{}'"), instance.handle.value);
        return failRebuild();
    }
    Core::BufferDesc bindlessSlotsBufferDesc;
    bindlessSlotsBufferDesc
        .setByteSize(sizeof(RuntimeBindlessResourceSlots))
        .setIsConstantBuffer(true)
        .setDebugName(bindlessSlotsBufferName)
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    rebuilt.bindlessResourceSlotsBuffer = m_graphics.createBuffer(bindlessSlotsBufferDesc);
    if(!rebuilt.bindlessResourceSlotsBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to create bindless slot buffer for runtime mesh '{}'"), instance.handle.value);
        return failRebuild();
    }

    if(!createRuntimeResourceBindlessHeapHandles(instance, rebuilt))
        return failRebuild();

    // Each rebuild allocates fresh heap slots. Free the replaced generation before dropping its BufferHandles so the
    // heap's deferred retirement keeps in-flight dispatches valid instead of overwriting their descriptors.
    releaseRuntimeResourceBindlessHeapHandles(resources);
    resources = Move(rebuilt);
    outResources = &resources;
    outResourcesRebuilt = true;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

