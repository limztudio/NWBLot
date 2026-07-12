
#include <impl/ecs_render/kernel/renderer_private.h>

#include <impl/ecs_render/kernel/arena_names.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_material_instance{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool writeMaterialInstanceOverrideBytes(
    const Core::ECS::EntityID entity,
    const Name& materialName,
    const MaterialInstanceParameter& parameter,
    const MaterialTypedLayoutField& field,
    const u32 byteOffset,
    Vector<u8, Core::Alloc::ScratchArena>& inOutMutableTypedBytes
){
    const u32 fieldByteSize = MaterialLayoutFieldByteSize(field.fieldType);
    if(fieldByteSize == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material instance override '{}' for entity {} has invalid field size")
            , StringConvert(parameter.parameterName.c_str())
            , entity.id
        );
        return false;
    }
    if(
        byteOffset > inOutMutableTypedBytes.size()
        || static_cast<usize>(fieldByteSize) > inOutMutableTypedBytes.size() - static_cast<usize>(byteOffset)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material instance override '{}' for entity {} exceeds mutable storage for material '{}'")
            , StringConvert(parameter.parameterName.c_str())
            , entity.id
            , StringConvert(materialName.c_str())
        );
        return false;
    }

    const u8* valueBytes = reinterpret_cast<const u8*>(parameter.value.raw);
    NWB_MEMCPY(inOutMutableTypedBytes.data() + byteOffset, fieldByteSize, valueBytes, fieldByteSize);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererMaterialSystem::findMaterialInstanceOverrideField(
    const Core::ECS::EntityID entity,
    const MaterialSurfaceInfo& materialInfo,
    const MaterialInstanceParameter& parameter,
    RendererMaterialInstanceOverrideField& outField
){
    outField = {};

    u32 constantBlockByteBegin = 0u;
    u32 mutableBlockByteBegin = 0u;
    for(const MaterialTypedLayoutBlock& block : materialInfo.typedLayoutBlocks){
        if(!IsValidMaterialBlockClass(block.blockClass)){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' has invalid typed material block class")
                , StringConvert(materialInfo.materialName.c_str())
            );
            return false;
        }

        const bool mutableBlock = block.blockClass == MaterialBlockClass::MaterialMutable;
        const u32 blockByteBegin = mutableBlock ? mutableBlockByteBegin : constantBlockByteBegin;
        u32& blockByteEnd = mutableBlock ? mutableBlockByteBegin : constantBlockByteBegin;
        if(block.byteSize > Limit<u32>::s_Max - blockByteEnd){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' typed material block byte range exceeds u32")
                , StringConvert(materialInfo.materialName.c_str())
            );
            return false;
        }
        blockByteEnd += block.byteSize;

        if(block.blockName != parameter.blockName)
            continue;

        const usize fieldBegin = static_cast<usize>(block.fieldBegin);
        const usize fieldCount = static_cast<usize>(block.fieldCount);
        if(fieldBegin > materialInfo.typedLayoutFields.size() || fieldCount > materialInfo.typedLayoutFields.size() - fieldBegin){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' typed material block field range is invalid")
                , StringConvert(materialInfo.materialName.c_str())
            );
            return false;
        }

        for(usize fieldIndex = fieldBegin; fieldIndex < fieldBegin + fieldCount; ++fieldIndex){
            const MaterialTypedLayoutField& field = materialInfo.typedLayoutFields[fieldIndex];
            if(field.fieldName != parameter.fieldName)
                continue;

            outField.field = &field;
            outField.blockByteBegin = blockByteBegin;
            outField.mutableBlock = mutableBlock;
            return true;
        }
        break;
    }

    NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material instance override '{}' for entity {} is not declared by material '{}'")
        , StringConvert(parameter.parameterName.c_str())
        , entity.id
        , StringConvert(materialInfo.materialName.c_str())
    );
    return false;
}

bool RendererMaterialSystem::applyMaterialInstanceOverrides(
    const Core::ECS::EntityID entity,
    const MaterialSurfaceInfo& materialInfo,
    const MaterialInstanceComponent& materialInstance,
    MaterialTypedByteDataVector& inOutMutableTypedBytes
){
    if(!materialInstance.materialInterface){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material instance overrides for entity {} require a material interface")
            , entity.id
        );
        return false;
    }
    if(materialInstance.materialInterface != materialInfo.materialInterface){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material instance overrides for entity {} target interface '{}' but material '{}' uses '{}'")
            , entity.id
            , StringConvert(materialInstance.materialInterface.c_str())
            , StringConvert(materialInfo.materialName.c_str())
            , StringConvert(materialInfo.materialInterface.c_str())
        );
        return false;
    }

    for(const MaterialInstanceParameter& parameter : materialInstance.overrides){
        if(!parameter.blockName || !parameter.fieldName){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material instance override for entity {} has an invalid parameter name")
                , entity.id
            );
            return false;
        }

        RendererMaterialInstanceOverrideField resolvedField;
        if(!findMaterialInstanceOverrideField(entity, materialInfo, parameter, resolvedField))
            return false;

        const MaterialTypedLayoutField& field = *resolvedField.field;
        if(!resolvedField.mutableBlock){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material instance override '{}' for entity {} targets material-constant storage")
                , StringConvert(parameter.parameterName.c_str())
                , entity.id
            );
            return false;
        }
        if(field.fieldType != parameter.fieldType){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material instance override '{}' for entity {} type does not match material '{}'")
                , StringConvert(parameter.parameterName.c_str())
                , entity.id
                , StringConvert(materialInfo.materialName.c_str())
            );
            return false;
        }

        if(field.offset > Limit<u32>::s_Max - resolvedField.blockByteBegin){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material instance override '{}' for entity {} byte offset exceeds u32")
                , StringConvert(parameter.parameterName.c_str())
                , entity.id
            );
            return false;
        }

        const u32 fieldByteOffset = resolvedField.blockByteBegin + field.offset;
        if(!__hidden_material_instance::writeMaterialInstanceOverrideBytes(
            entity,
            materialInfo.materialName,
            parameter,
            field,
            fieldByteOffset,
            inOutMutableTypedBytes
        ))
            return false;
    }

    return true;
}

bool RendererMaterialSystem::resolveMaterialInstanceMutableTypedBytes(
    const Core::ECS::EntityID entity,
    const MaterialSurfaceInfo& materialInfo,
    const MaterialInstanceComponent* materialInstance,
    const MaterialTypedByteVector*& outMutableTypedBytes
){
    pruneMaterialInstanceMutableCache();

    outMutableTypedBytes = nullptr;
    if(!materialInstance || materialInstance->overrides.empty()){
        outMutableTypedBytes = &materialInfo.mutableDefaultTypedBytes;
        return true;
    }

    auto it = materialState().m_instanceMutableCache.try_emplace(entity, arena()).first;
    MaterialInstanceMutableCacheEntry& cacheEntry = it.value();
    if(
        cacheEntry.materialName == materialInfo.materialName
        && cacheEntry.materialInterface == materialInfo.materialInterface
        && materialInstance->materialInterface == materialInfo.materialInterface
        && cacheEntry.typedLayoutHash == materialInfo.typedLayoutHash
        && cacheEntry.revision == materialInstance->revision
    ){
        outMutableTypedBytes = &cacheEntry.mutableTypedBytes;
        return true;
    }

    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_MutableTypedBytesArena);
    MaterialTypedByteDataVector mutableTypedBytes{scratchArena};
    mutableTypedBytes.assign(materialInfo.mutableDefaultTypedBytes.begin(), materialInfo.mutableDefaultTypedBytes.end());
    if(!applyMaterialInstanceOverrides(entity, materialInfo, *materialInstance, mutableTypedBytes)){
        materialState().m_instanceMutableCache.erase(it);
        return false;
    }

    cacheEntry.materialName = materialInfo.materialName;
    cacheEntry.materialInterface = materialInfo.materialInterface;
    cacheEntry.typedLayoutHash = materialInfo.typedLayoutHash;
    cacheEntry.revision = materialInstance->revision;
    AssignTriviallyCopyableVector(cacheEntry.mutableTypedBytes, mutableTypedBytes);

    outMutableTypedBytes = &cacheEntry.mutableTypedBytes;
    return true;
}

bool RendererMaterialSystem::appendShadowOccluderMaterialContext(
    const Core::ECS::EntityID entity,
    const MaterialSurfaceInfo& materialInfo,
    const NWB::Impl::Scene::TransformComponent* transform,
    MaterialTypedByteDataVector& inOutMaterialTypedBytes,
    ECSRenderDetail::MaterialTypedByteContentRangeMap& inOutMutableRanges,
    InstanceGpuData& outInstance,
    u32& outConstantByteOffset
){
    outInstance = InstanceGpuData{};
    outConstantByteOffset = 0u;

    // Constant block: appended raw (its content is per-material, not deduped against other materials' blocks),
    // exactly as the draw pass's appendConstantMaterialTypedBytes does, so its byte offset locates this
    // occluder's constant block in the combined buffer.
    ECSRenderDetail::MaterialTypedInstanceRanges typedRanges;
    if(!ECSRenderDetail::AppendMaterialTypedByteRange(
        inOutMaterialTypedBytes,
        materialInfo.constantTypedBytes,
        typedRanges.constantRange
    ))
        return false;

    // Mutable block: the per-instance override bytes (or the material default), content-deduped so instances
    // sharing identical mutable storage share one appended range -- mirroring the draw pass.
    const MaterialInstanceComponent* materialInstance = world().tryGetComponent<MaterialInstanceComponent>(entity);
    const MaterialTypedByteVector* mutableTypedBytes = nullptr;
    if(!resolveMaterialInstanceMutableTypedBytes(entity, materialInfo, materialInstance, mutableTypedBytes))
        return false;
    if(!ECSRenderDetail::FindOrAppendMaterialTypedByteRange(
        inOutMaterialTypedBytes,
        inOutMutableRanges,
        *mutableTypedBytes,
        typedRanges.mutableRange
    ))
        return false;

    outInstance = ECSRenderDetail::BuildInstanceGpuData(transform, typedRanges);
    outConstantByteOffset = typedRanges.constantRange.byteOffset;
    return true;
}

void RendererMaterialSystem::pruneMaterialInstanceMutableCache(){
    const u64 componentMutationVersion = world().componentMutationVersion<MaterialInstanceComponent>();
    if(componentMutationVersion == materialState().m_instanceMutableCacheComponentMutationVersion)
        return;

    materialState().m_instanceMutableCache.clear();
    materialState().m_instanceMutableCacheComponentMutationVersion = componentMutationVersion;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

