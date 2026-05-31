// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "system.h"

#include "runtime_cache.h"
#include "resource_names.h"
#include "skin_payload.h"

#include <core/common/log.h>
#include <core/graphics/module.h>
#include <impl/assets/graphics/skinned_mesh/binding_slots.h>
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
        skinningBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_SKINNED_MESH_BINDING_REST_POSITION, instance.restPositionBuffer.get()));
        skinningBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_SKINNED_MESH_BINDING_SKINNED_POSITION, instance.skinnedPositionBuffer.get()));
        skinningBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_SKINNED_MESH_BINDING_REST_NORMAL, instance.restNormalBuffer.get()));
        skinningBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_SKINNED_MESH_BINDING_SKINNED_NORMAL, instance.skinnedNormalBuffer.get()));
        skinningBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_SKINNED_MESH_BINDING_REST_TANGENT, instance.restTangentBuffer.get()));
        skinningBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_SKINNED_MESH_BINDING_SKINNED_TANGENT, instance.skinnedTangentBuffer.get()));
        skinningBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_SKINNED_MESH_BINDING_MESHLET_DESC, instance.meshletDescBuffer.get()));
        skinningBindingSetDesc.addItem(Core::BindingSetItem::RawBuffer_SRV(NWB_SKINNED_MESH_BINDING_POSITION_REF_DELTAS, instance.meshletPositionRefDeltaBuffer.get()));
        skinningBindingSetDesc.addItem(Core::BindingSetItem::RawBuffer_SRV(NWB_SKINNED_MESH_BINDING_ATTRIBUTE_REF_DELTAS, instance.meshletAttributeRefDeltaBuffer.get()));
        skinningBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_SKINNED_MESH_BINDING_ATTRIBUTE_SKINS, instance.attributeSkinBuffer.get()));
        skinningBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_SKINNED_MESH_BINDING_SKIN_INFLUENCES, rebuilt.skinBuffer.get()));
        skinningBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_SKINNED_MESH_BINDING_JOINT_PALETTE, rebuilt.jointPaletteBuffer.get()));
        rebuilt.skinningBindingSet = device->createBindingSet(skinningBindingSetDesc, m_skinningBindingLayout);
        if(!rebuilt.skinningBindingSet){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshSystem: failed to create skinning binding set for runtime mesh '{}'"), instance.handle.value);
            return false;
        }
    }

    Core::BindingSetDesc boundsBindingSetDesc(m_arena);
    boundsBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_SKINNED_MESH_BOUNDS_BINDING_POSITIONS, instance.skinnedPositionBuffer.get()));
    boundsBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_SKINNED_MESH_BOUNDS_BINDING_MESHLET_DESC, instance.meshletDescBuffer.get()));
    boundsBindingSetDesc.addItem(Core::BindingSetItem::RawBuffer_SRV(NWB_SKINNED_MESH_BOUNDS_BINDING_POSITION_REF_DELTAS, instance.meshletPositionRefDeltaBuffer.get()));
    boundsBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_SKINNED_MESH_BOUNDS_BINDING_LOCAL_VERTEX_REFS, instance.meshletLocalVertexRefBuffer.get()));
    boundsBindingSetDesc.addItem(Core::BindingSetItem::RawBuffer_SRV(NWB_SKINNED_MESH_BOUNDS_BINDING_PRIMITIVE_INDICES, instance.meshletPrimitiveIndexBuffer.get()));
    boundsBindingSetDesc.addItem(Core::BindingSetItem::RawBuffer_UAV(NWB_SKINNED_MESH_BOUNDS_BINDING_DYNAMIC_BOUNDS, instance.meshletBoundsBuffer.get()));
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

