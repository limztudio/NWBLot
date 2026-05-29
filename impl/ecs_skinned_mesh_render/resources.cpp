// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "system.h"

#include "runtime_cache.h"
#include "resource_names.h"
#include "skin_payload.h"

#include <core/common/log.h>
#include <core/graphics/module.h>
#include <impl/ecs_mesh_runtime/buffer_upload.h>


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
    if(!RuntimeMeshBufferUpload::PayloadByteCountFits<PayloadT>(count)){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshSystem: {} payload byte size overflows"), label);
        return {};
    }

    return RuntimeMeshBufferUpload::SetupBuffer<PayloadT>(
        graphics,
        debugName,
        payload,
        count
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool SkinnedMeshSystem::ensureRuntimeResources(
    SkinnedMeshRuntimeMeshInstance& instance,
    const RuntimePayloadViews& payloadViews,
    RuntimeResources*& outResources,
    bool& outResourcesRebuilt
){
    outResources = nullptr;
    outResourcesRebuilt = false;
#if defined(NWB_DEBUG)
    if((payloadViews.skinInfluenceCount == 0u) != (payloadViews.jointPaletteCount == 0u)){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshSystem: runtime mesh '{}' has mismatched skin influence/joint payloads"), instance.handle.value);
        return false;
    }
#endif

    const bool hasActiveSkin = payloadViews.hasActiveSkin();
#if defined(NWB_DEBUG)
    if(!payloadViews.skinInfluences || !payloadViews.jointPalette){
        if(hasActiveSkin){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshSystem: runtime mesh '{}' has null active skinned mesh payloads"), instance.handle.value);
            return false;
        }
    }

    if(
        payloadViews.skinInfluenceCount > static_cast<usize>(Limit<u32>::s_Max)
        || payloadViews.jointPaletteCount > static_cast<usize>(Limit<u32>::s_Max)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshSystem: runtime mesh '{}' skinned mesh payload exceeds u32 limits"), instance.handle.value);
        return false;
    }
#endif

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
        || !resources.boundsBindingSet
        || (hasActiveSkin && (!resources.skinBuffer || !resources.jointPaletteBuffer || !resources.skinningBindingSet))
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

    auto* device = m_graphics.getDevice();
    if(hasActiveSkin){
        const Name skinBufferName = DeriveRuntimeResourceName(instance.source.name(), instance.handle.value, instance.editRevision, "skinned_mesh_skin");
        const Name jointPaletteBufferName = DeriveRuntimeResourceName(instance.source.name(), instance.handle.value, instance.editRevision, "skinned_mesh_joints");
        if(!skinBufferName || !jointPaletteBufferName){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshSystem: failed to derive skinned mesh buffer names for runtime mesh '{}'"), instance.handle.value);
            return false;
        }

        rebuilt.skinBuffer = __hidden_resources::SetupStructuredBuffer(
            m_graphics,
            skinBufferName,
            payloadViews.skinInfluences,
            payloadViews.skinInfluenceCount,
            NWB_TEXT("skin influence")
        )
        ;
        if(!rebuilt.skinBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshSystem: failed to create skin buffer for runtime mesh '{}'"), instance.handle.value);
            return false;
        }

        rebuilt.jointPaletteBuffer = __hidden_resources::SetupStructuredBuffer(
            m_graphics,
            jointPaletteBufferName,
            payloadViews.jointPalette,
            payloadViews.jointPaletteCount,
            NWB_TEXT("joint palette")
        )
        ;
        if(!rebuilt.jointPaletteBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshSystem: failed to create joint palette buffer for runtime mesh '{}'"), instance.handle.value);
            return false;
        }

        Core::BindingSetDesc skinningBindingSetDesc(m_arena);
        skinningBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(0, instance.restPositionBuffer.get()));
        skinningBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(1, instance.skinnedPositionBuffer.get()));
        skinningBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(2, instance.restNormalBuffer.get()));
        skinningBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(3, instance.skinnedNormalBuffer.get()));
        skinningBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(4, instance.restTangentBuffer.get()));
        skinningBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(5, instance.skinnedTangentBuffer.get()));
        skinningBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(6, instance.meshletDescBuffer.get()));
        skinningBindingSetDesc.addItem(Core::BindingSetItem::RawBuffer_SRV(7, instance.meshletPositionRefDeltaBuffer.get()));
        skinningBindingSetDesc.addItem(Core::BindingSetItem::RawBuffer_SRV(8, instance.meshletAttributeRefDeltaBuffer.get()));
        skinningBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(9, instance.attributeSkinBuffer.get()));
        skinningBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(10, rebuilt.skinBuffer.get()));
        skinningBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(11, rebuilt.jointPaletteBuffer.get()));
        rebuilt.skinningBindingSet = device->createBindingSet(skinningBindingSetDesc, m_skinningBindingLayout);
        if(!rebuilt.skinningBindingSet){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshSystem: failed to create skinning binding set for runtime mesh '{}'"), instance.handle.value);
            return false;
        }
    }

    Core::BindingSetDesc boundsBindingSetDesc(m_arena);
    boundsBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(0, instance.skinnedPositionBuffer.get()));
    boundsBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(1, instance.meshletDescBuffer.get()));
    boundsBindingSetDesc.addItem(Core::BindingSetItem::RawBuffer_SRV(2, instance.meshletPositionRefDeltaBuffer.get()));
    boundsBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(3, instance.meshletLocalVertexRefBuffer.get()));
    boundsBindingSetDesc.addItem(Core::BindingSetItem::RawBuffer_SRV(4, instance.meshletPrimitiveIndexBuffer.get()));
    boundsBindingSetDesc.addItem(Core::BindingSetItem::RawBuffer_UAV(5, instance.meshletBoundsBuffer.get()));
    rebuilt.boundsBindingSet = device->createBindingSet(boundsBindingSetDesc, m_boundsBindingLayout);
    if(!rebuilt.boundsBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshSystem: failed to create bounds binding set for runtime mesh '{}'"), instance.handle.value);
        return false;
    }

    resources = Move(rebuilt);
    outResources = &resources;
    outResourcesRebuilt = true;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

