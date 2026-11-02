// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "system.h"
#include "skin_payload.h"

#include <impl/ecs_mesh/runtime/buffer_upload.h>

#include <core/common/log.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/runtime/runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_resources{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename PayloadT>
static Expected<Core::BufferHandle, BufferSetupFailure::Enum> SetupStructuredBuffer(Core::GraphicsRuntime& graphics, const Name& debugName, const Span<const PayloadT> payload, const TStringView label){
    if(MultiplyOverflows<usize>(payload.size(), sizeof(PayloadT))){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: {} payload byte size overflows"), label);
        return MakeUnexpected(BufferSetupFailure::ByteSizeOverflow);
    }

    return RuntimeMeshBufferUpload::SetupBuffer<PayloadT>(graphics, debugName, payload, { false, false, false, Core::ResourceQueueSharing::GraphicsAndAsyncCompute });
}

static Expected<Core::GpuDescriptorHandle> RegisterStorageBuffer(Core::GpuDescriptorHeap& heap, const Core::DescriptorWriteItem& item){
    const Expected<Core::GpuDescriptorHandle> handle = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
    if(!handle)
        return MakeUnexpected(handle.error());
    Core::GpuDescriptorRetirementGuard retireHandle(heap, *handle);
    if(!heap.write(*handle, item))
        return MakeUnexpected(Failure{});
    retireHandle.release();
    return *handle;
}

static Expected<Core::GpuDescriptorHandle> RegisterUniformBuffer(Core::GpuDescriptorHeap& heap, Core::Buffer& buffer){
    const Expected<Core::GpuDescriptorHandle> handle = heap.allocate(Core::GpuDescriptorClass::UniformBuffer);
    if(!handle)
        return MakeUnexpected(handle.error());
    Core::GpuDescriptorRetirementGuard retireHandle(heap, *handle);
    if(!heap.write(*handle, Core::DescriptorWriteItem::ConstantBuffer(0u, &buffer)))
        return MakeUnexpected(Failure{});
    retireHandle.release();
    return *handle;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<b8> MeshSkinningSystem::ensureRuntimeResources(MeshSkinningRuntimeInstance& instance, Core::Alloc::ScratchArena& scratchArena){
    const b8 hasSkinPayload = !instance.skin.empty();
    if(instance.skin.size() > static_cast<usize>(Limit<u32>::s_Max) || (hasSkinPayload && (instance.skeletonJointCount == 0u || instance.skeletonJointCount > static_cast<u32>(Limit<u16>::s_Max) + 1u))){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: runtime mesh '{}' has invalid skin resource counts"), instance.handle.value);
        return MakeUnexpected(Failure{});
    }

    MeshSkinningResourceBuffers buffers = CaptureMeshSkinningResourceBuffers(instance);
    auto [it, inserted] = m_runtimeResources.try_emplace(instance.handle.value);
    RuntimeResources& resources = it.value();
    const u32 positionCount = instance.meshletPositionRefCount;
    const u32 attributeCount = instance.meshletAttributeRefCount;
    const u32 meshletCount = static_cast<u32>(instance.meshlets.size());
    const u32 skinCount = static_cast<u32>(instance.skin.size());
    const u32 jointCount = hasSkinPayload ? instance.skeletonJointCount : 0u;
    const b8 rebuild =
        inserted
        || resources.buffers != buffers
        || resources.editRevision != instance.editRevision
        || resources.positionCount != positionCount
        || resources.attributeCount != attributeCount
        || resources.meshletCount != meshletCount
        || resources.skinCount != skinCount
        || resources.jointCount != jointCount
        || !resources.hasPersistentHeapDescriptors(hasSkinPayload, instance.attributeBuffer != nullptr)
        || (hasSkinPayload && (!resources.skinBuffer || !resources.jointPaletteBuffer))
    ;
    if(!rebuild)
        return false;

    class UnpublishedRuntimeDescriptors final : NoCopy{
    public:
        UnpublishedRuntimeDescriptors(MeshSkinningSystem& system, RuntimeResources& resources)noexcept
            : m_system(system)
            , m_resources(resources)
        {}
        ~UnpublishedRuntimeDescriptors()noexcept(noexcept(DeclVal<MeshSkinningSystem&>().releaseRuntimeResourceBindlessHeapHandles(DeclVal<RuntimeResources&>()))){
            if(m_active)
                m_system.releaseRuntimeResourceBindlessHeapHandles(m_resources);
        }


    public:
        void release()noexcept{ m_active = false; }


    private:
        MeshSkinningSystem& m_system;
        RuntimeResources& m_resources;
        b8 m_active = true;
    };

    RuntimeResources rebuilt;
    UnpublishedRuntimeDescriptors retireRebuilt(*this, rebuilt);
    rebuilt.handle = instance.handle;
    rebuilt.buffers = Move(buffers);
    rebuilt.editRevision = instance.editRevision;
    rebuilt.positionCount = positionCount;
    rebuilt.attributeCount = attributeCount;
    rebuilt.meshletCount = meshletCount;
    rebuilt.skinCount = skinCount;
    rebuilt.jointCount = jointCount;

    if(hasSkinPayload){
        const auto skinInfluences = MeshSkinningPayload::BuildSkinInfluences(instance, scratchArena);
        if(!skinInfluences)
            return MakeUnexpected(Failure{});

        const auto skinBufferName = DeriveRuntimeResourceName(instance.sourceName, instance.handle.value, instance.editRevision, s_SkinBufferSuffix);
        const auto jointPaletteBufferName = DeriveRuntimeResourceName(instance.sourceName, instance.handle.value, instance.editRevision, s_JointPaletteBufferSuffix);
        if(!skinBufferName || !jointPaletteBufferName){
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to derive skinning buffer names for runtime mesh '{}'"), instance.handle.value);
            return MakeUnexpected(Failure{});
        }

        auto skinBufferResult = __hidden_resources::SetupStructuredBuffer<MeshSkinningInfluenceGpu>(m_graphics, skinBufferName, *skinInfluences, NWB_TEXT("skin influence"));
        if(!skinBufferResult){
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to create skin buffer for runtime mesh '{}'"), instance.handle.value);
            return MakeUnexpected(Failure{});
        }
        rebuilt.skinBuffer = Move(*skinBufferResult);

        // Live pose data arrives from the graph after ECS updates; setup owns only the Skin-sized storage.
        Core::BufferDesc jointPaletteBufferDesc;
        jointPaletteBufferDesc
            .setByteSize(static_cast<u64>(jointCount) * sizeof(SkeletonJointMatrix))
            .setStructStride(sizeof(SkeletonJointMatrix))
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
            .setDebugName(jointPaletteBufferName)
            .setInitialState(Core::ResourceStates::Common)
        ;
        auto jointPaletteBufferResult = m_graphics.createBuffer(jointPaletteBufferDesc);
        if(!jointPaletteBufferResult){
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to create joint palette buffer for runtime mesh '{}'"), instance.handle.value);
            return MakeUnexpected(Failure{});
        }
        rebuilt.jointPaletteBuffer = Move(*jointPaletteBufferResult);
    }

    const auto bindlessSlotsBufferName = DeriveRuntimeResourceName(instance.sourceName, instance.handle.value, instance.editRevision, s_BindlessSlotsBufferSuffix);
    if(!bindlessSlotsBufferName){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to derive bindless slot buffer name for runtime mesh '{}'"), instance.handle.value);
        return MakeUnexpected(Failure{});
    }
    Core::BufferDesc bindlessSlotsBufferDesc;
    bindlessSlotsBufferDesc
        .setByteSize(sizeof(MeshSkinningBindlessResourceSlots))
        .setIsConstantBuffer(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setDebugName(bindlessSlotsBufferName)
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    auto bindlessResourceSlotsBufferResult = m_graphics.createBuffer(bindlessSlotsBufferDesc);
    if(bindlessResourceSlotsBufferResult)
        rebuilt.bindlessResourceSlotsBuffer = Move(*bindlessResourceSlotsBufferResult);
    else
        rebuilt.bindlessResourceSlotsBuffer.reset();
    if(!bindlessResourceSlotsBufferResult){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to create bindless slot buffer for runtime mesh '{}'"), instance.handle.value);
        return MakeUnexpected(Failure{});
    }

    if(!createRuntimeResourceBindlessHeapHandles(instance, rebuilt))
        return MakeUnexpected(Failure{});

    // Rebuilds allocate fresh slots; free the replaced generation before dropping its handles.
    releaseRuntimeResourceBindlessHeapHandles(resources);
    resources = Move(rebuilt);
    retireRebuilt.release();
    return true;
}

b8 MeshSkinningSystem::createRuntimeResourceBindlessHeapHandles(MeshSkinningRuntimeInstance& instance, RuntimeResources& resources){
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
    if(!resources.bindlessResourceSlotsBuffer || !instance.hasPersistentComputeBuffers()){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: runtime mesh '{}' has incomplete persistent compute buffers"), instance.handle.value);
        return false;
    }

    const auto registerStorageSlot = [&](const RuntimeStorageDescriptorSlot::Enum slot, const Core::DescriptorWriteItem& item, u32& outSlot){
        const auto handle = __hidden_resources::RegisterStorageBuffer(heap, item);
        if(!handle)
            return false;
        resources.bindlessHeapHandles.storageBuffers[static_cast<usize>(slot)] = *handle;
        outSlot = handle->slot();
        return true;
    };

    struct StorageDescriptorBinding{
        RuntimeStorageDescriptorSlot::Enum slot;
        Core::DescriptorWriteItem item;
        u32* outSlot;
    };
    MeshSkinningBindlessResourceSlots& slots = resources.bindlessResourceSlots;
    const Array<StorageDescriptorBinding, s_RuntimeRequiredStorageDescriptorCount> requiredBindings = {{
        { RuntimeStorageDescriptorSlot::RestPosition, Core::DescriptorWriteItem::StructuredBufferSrv(0u, instance.restPositionBuffer.get()), &slots.restPosition },
        { RuntimeStorageDescriptorSlot::SkinnedPosition, Core::DescriptorWriteItem::StructuredBufferUav(0u, instance.skinnedPositionBuffer.get()), &slots.skinnedPosition },
        { RuntimeStorageDescriptorSlot::RestNormal, Core::DescriptorWriteItem::StructuredBufferSrv(0u, instance.restNormalBuffer.get()), &slots.restNormal },
        { RuntimeStorageDescriptorSlot::SkinnedNormal, Core::DescriptorWriteItem::StructuredBufferUav(0u, instance.skinnedNormalBuffer.get()), &slots.skinnedNormal },
        { RuntimeStorageDescriptorSlot::RestTangent, Core::DescriptorWriteItem::StructuredBufferSrv(0u, instance.restTangentBuffer.get()), &slots.restTangent },
        { RuntimeStorageDescriptorSlot::SkinnedTangent, Core::DescriptorWriteItem::StructuredBufferUav(0u, instance.skinnedTangentBuffer.get()), &slots.skinnedTangent },
        { RuntimeStorageDescriptorSlot::MeshletDesc, Core::DescriptorWriteItem::StructuredBufferSrv(0u, instance.meshletDescBuffer.get()), &slots.meshletDesc },
        { RuntimeStorageDescriptorSlot::PositionRefDeltas, Core::DescriptorWriteItem::RawBufferSrv(0u, instance.meshletPositionRefDeltaBuffer.get()), &slots.positionRefDeltas },
        { RuntimeStorageDescriptorSlot::AttributeRefDeltas, Core::DescriptorWriteItem::RawBufferSrv(0u, instance.meshletAttributeRefDeltaBuffer.get()), &slots.attributeRefDeltas },
        { RuntimeStorageDescriptorSlot::AttributeSkins, Core::DescriptorWriteItem::StructuredBufferSrv(0u, instance.attributeSkinBuffer.get()), &slots.attributeSkins },
        { RuntimeStorageDescriptorSlot::LocalVertexRefs, Core::DescriptorWriteItem::StructuredBufferSrv(0u, instance.meshletLocalVertexRefBuffer.get()), &slots.localVertexRefs },
        { RuntimeStorageDescriptorSlot::PrimitiveIndices, Core::DescriptorWriteItem::RawBufferSrv(0u, instance.meshletPrimitiveIndexBuffer.get()), &slots.primitiveIndices },
        { RuntimeStorageDescriptorSlot::MeshletBounds, Core::DescriptorWriteItem::RawBufferUav(0u, instance.meshletBoundsBuffer.get()), &slots.meshletBounds },
        { RuntimeStorageDescriptorSlot::MeshletLocalBounds, Core::DescriptorWriteItem::RawBufferUav(0u, instance.meshletLocalBoundsBuffer.get()), &slots.meshletLocalBounds },
        { RuntimeStorageDescriptorSlot::LocalBounds, Core::DescriptorWriteItem::RawBufferUav(0u, instance.localBoundsBuffer.get()), &slots.localBounds },
    }};
    for(const StorageDescriptorBinding& binding : requiredBindings)
        if(!registerStorageSlot(binding.slot, binding.item, *binding.outSlot))
            return fail();

    if(resources.skinCount != 0u){
        if(!resources.skinBuffer || !resources.jointPaletteBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: runtime mesh '{}' has incomplete persistent skinning payload buffers"), instance.handle.value);
            return fail();
        }
        if(
            !registerStorageSlot(RuntimeStorageDescriptorSlot::SkinInfluences, Core::DescriptorWriteItem::StructuredBufferSrv(0u, resources.skinBuffer.get()), slots.skinInfluences)
            || !registerStorageSlot(RuntimeStorageDescriptorSlot::JointPalette, Core::DescriptorWriteItem::StructuredBufferSrv(0u, resources.jointPaletteBuffer.get()), slots.jointPalette)
        )
            return fail();
    }

    if(instance.attributeBuffer && !registerStorageSlot(RuntimeStorageDescriptorSlot::AttributeBuffer, Core::DescriptorWriteItem::RawBufferUav(0u, instance.attributeBuffer.get()), slots.attributeBuffer))
        return fail();

    const auto resourceSlots = __hidden_resources::RegisterUniformBuffer(heap, *resources.bindlessResourceSlotsBuffer.get());
    if(!resourceSlots)
        return fail();
    resources.bindlessHeapHandles.resourceSlots = *resourceSlots;
    return true;
}

void MeshSkinningSystem::releaseRuntimeResourceBindlessHeapHandles(RuntimeResources& resources){
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    if(heap.isInitialized()){
        const auto release = [&](Core::GpuDescriptorHandle& handle){
            if(handle.valid())
                heap.free(handle);
            handle = Core::GpuDescriptorHandle::Invalid();
        };
        release(resources.bindlessHeapHandles.resourceSlots);
        for(Core::GpuDescriptorHandle& handle : resources.bindlessHeapHandles.storageBuffers)
            release(handle);
    }
    resources.bindlessHeapHandles = RuntimeBindlessHeapHandles{};
    resources.bindlessResourceSlots = MeshSkinningBindlessResourceSlots{};
    resources.bindlessResourceSlotsUploaded = false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

