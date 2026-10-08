// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "material_instance.h"

#include <impl/ecs_render/material/material_system.h>
#include <impl/ecs_render/material/renderer_material_state.h>
#include <impl/ecs_render/kernel/arena_names.h>

#include <core/common/log.h>
#include <core/ecs/world.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_material_instance{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool WriteMaterialInstanceOverrideBytes(
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
            , StringConvert(parameter.parameterName.resolvedText())
            , entity.id
        );
        return false;
    }
    if(
        byteOffset > inOutMutableTypedBytes.size()
        || static_cast<usize>(fieldByteSize) > inOutMutableTypedBytes.size() - static_cast<usize>(byteOffset)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material instance override '{}' for entity {} exceeds mutable storage for material '{}'")
            , StringConvert(parameter.parameterName.resolvedText())
            , entity.id
            , StringConvert(materialName.resolvedText())
        );
        return false;
    }

    const u8* valueBytes = reinterpret_cast<const u8*>(parameter.value.raw);
    NWB_MEMCPY(inOutMutableTypedBytes.data() + byteOffset, fieldByteSize, valueBytes, fieldByteSize);
    return true;
}


// Single source of truth for cache validity shared by prepare and find paths.
[[nodiscard]] static bool MaterialInstanceMutableCacheEntryMatches(
    const MaterialInstanceMutableCacheEntry& cacheEntry,
    const MaterialSurfaceInfo& materialInfo,
    const MaterialInstanceComponent& materialInstance
)noexcept{
    return cacheEntry.materialName == materialInfo.materialName
        && cacheEntry.materialInterface == materialInfo.materialInterface
        && materialInstance.materialInterface == materialInfo.materialInterface
        && cacheEntry.typedLayoutHash == materialInfo.typedLayoutHash
        && cacheEntry.revision == materialInstance.revision
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<MaterialInstanceOverrideField> RendererMaterialSystem::FindMaterialInstanceOverrideField(
    const Core::ECS::EntityID entity,
    const MaterialSurfaceInfo& materialInfo,
    const MaterialInstanceParameter& parameter
){
    u32 constantBlockByteBegin = 0u;
    u32 mutableBlockByteBegin = 0u;
    for(const MaterialTypedLayoutBlock& block : materialInfo.typedLayoutBlocks){
        // MaterialSurfaceInfo copies the already-validated cooked layout; keep a debug-only invariant here.
        NWB_ASSERT(IsValidMaterialBlockClass(block.blockClass));

        const bool mutableBlock = block.blockClass == MaterialBlockClass::MaterialMutable;
        const u32 blockByteBegin = mutableBlock ? mutableBlockByteBegin : constantBlockByteBegin;
        u32& blockByteEnd = mutableBlock ? mutableBlockByteBegin : constantBlockByteBegin;
        // MaterialSurfaceInfo copies the already-validated cooked byte sizes; keep a debug-only invariant here.
        NWB_ASSERT(block.byteSize <= Limit<u32>::s_Max - blockByteEnd);
        blockByteEnd += block.byteSize;

        if(block.blockName != parameter.blockName)
            continue;

        const usize fieldBegin = static_cast<usize>(block.fieldBegin);
        const usize fieldCount = static_cast<usize>(block.fieldCount);
        // MaterialSurfaceInfo copies the already-validated cooked field ranges; keep a debug-only invariant here.
        NWB_ASSERT(fieldBegin <= materialInfo.typedLayoutFields.size() && fieldCount <= materialInfo.typedLayoutFields.size() - fieldBegin);

        for(usize fieldIndex = fieldBegin; fieldIndex < fieldBegin + fieldCount; ++fieldIndex){
            const MaterialTypedLayoutField& field = materialInfo.typedLayoutFields[fieldIndex];
            if(field.fieldName != parameter.fieldName)
                continue;

            return MaterialInstanceOverrideField{ &field, blockByteBegin, mutableBlock };
        }
        break;
    }

    NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material instance override '{}' for entity {} is not declared by material '{}'")
        , StringConvert(parameter.parameterName.resolvedText())
        , entity.id
        , StringConvert(materialInfo.materialName.resolvedText())
    );
    return MakeUnexpected(Failure{});
}

bool RendererMaterialSystem::ApplyMaterialInstanceOverrides(
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
            , StringConvert(materialInstance.materialInterface.resolvedText())
            , StringConvert(materialInfo.materialName.resolvedText())
            , StringConvert(materialInfo.materialInterface.resolvedText())
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

        const auto resolvedField = FindMaterialInstanceOverrideField(entity, materialInfo, parameter);
        if(!resolvedField)
            return false;

        const MaterialTypedLayoutField& field = *resolvedField->field;
        if(!resolvedField->mutableBlock){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material instance override '{}' for entity {} targets material-constant storage")
                , StringConvert(parameter.parameterName.resolvedText())
                , entity.id
            );
            return false;
        }
        if(field.fieldType != parameter.fieldType){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material instance override '{}' for entity {} type does not match material '{}'")
                , StringConvert(parameter.parameterName.resolvedText())
                , entity.id
                , StringConvert(materialInfo.materialName.resolvedText())
            );
            return false;
        }

        if(field.offset > Limit<u32>::s_Max - resolvedField->blockByteBegin){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material instance override '{}' for entity {} byte offset exceeds u32")
                , StringConvert(parameter.parameterName.resolvedText())
                , entity.id
            );
            return false;
        }

        const u32 fieldByteOffset = resolvedField->blockByteBegin + field.offset;
        if(!__hidden_material_instance::WriteMaterialInstanceOverrideBytes(
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

Expected<const MaterialTypedByteVector*> RendererMaterialSystem::prepareMaterialInstanceMutableTypedBytes(
    const Core::ECS::EntityID entity,
    const MaterialSurfaceInfo& materialInfo,
    const MaterialInstanceComponent* materialInstance
){
    pruneMaterialInstanceMutableCache();

    if(!materialInstance || materialInstance->overrides.empty()){
        return &materialInfo.mutableDefaultTypedBytes;
    }

    auto it = m_materialState.m_instanceMutableCache.try_emplace(entity, m_arena).first;
    MaterialInstanceMutableCacheEntry& cacheEntry = it.value();
    if(__hidden_material_instance::MaterialInstanceMutableCacheEntryMatches(cacheEntry, materialInfo, *materialInstance)){
        return &cacheEntry.mutableTypedBytes;
    }

    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_MutableTypedBytesArena);
    MaterialTypedByteDataVector mutableTypedBytes{scratchArena};
    mutableTypedBytes.reserve(materialInfo.mutableDefaultTypedBytes.size());
    mutableTypedBytes.assign(materialInfo.mutableDefaultTypedBytes.begin(), materialInfo.mutableDefaultTypedBytes.end());
    if(!ApplyMaterialInstanceOverrides(entity, materialInfo, *materialInstance, mutableTypedBytes)){
        m_materialState.m_instanceMutableCache.erase(it);
        return MakeUnexpected(Failure{});
    }

    cacheEntry.materialName = materialInfo.materialName;
    cacheEntry.materialInterface = materialInfo.materialInterface;
    cacheEntry.typedLayoutHash = materialInfo.typedLayoutHash;
    cacheEntry.revision = materialInstance->revision;
    AssignTriviallyCopyableVector(cacheEntry.mutableTypedBytes, mutableTypedBytes);

    return &cacheEntry.mutableTypedBytes;
}

Expected<const MaterialTypedByteVector*> RendererMaterialSystem::findPreparedMaterialInstanceMutableTypedBytes(
    const Core::ECS::EntityID entity,
    const MaterialSurfaceInfo& materialInfo,
    const MaterialInstanceComponent* materialInstance
)const{
    if(!materialInstance || materialInstance->overrides.empty()){
        return &materialInfo.mutableDefaultTypedBytes;
    }

    const auto found = m_materialState.m_instanceMutableCache.find(entity);
    if(found == m_materialState.m_instanceMutableCache.end())
        return MakeUnexpected(Failure{});

    const MaterialInstanceMutableCacheEntry& cacheEntry = found.value();
    if(!__hidden_material_instance::MaterialInstanceMutableCacheEntryMatches(cacheEntry, materialInfo, *materialInstance))
        return MakeUnexpected(Failure{});

    return &cacheEntry.mutableTypedBytes;
}

Expected<ShadowOccluderMaterialContext> RendererMaterialSystem::appendShadowOccluderMaterialContext(
    const Core::ECS::EntityID entity,
    const MaterialSurfaceInfo& materialInfo,
    const NWB::Impl::Scene::TransformComponent* transform,
    MaterialTypedByteDataVector& inOutMaterialTypedBytes,
    ECSRenderDetail::MaterialTypedByteContentRangeMap& inOutMutableRanges
){
    const auto constantRange = ECSRenderDetail::AppendMaterialTypedByteRange(inOutMaterialTypedBytes, materialInfo.constantTypedBytes);
    if(!constantRange)
        return MakeUnexpected(Failure{});

    const MaterialInstanceComponent* materialInstance = m_world.tryGetComponent<MaterialInstanceComponent>(entity);
    const auto mutableTypedBytes = prepareMaterialInstanceMutableTypedBytes(entity, materialInfo, materialInstance);
    if(!mutableTypedBytes)
        return MakeUnexpected(Failure{});
    const auto mutableRange = ECSRenderDetail::FindOrAppendMaterialTypedByteRange(inOutMaterialTypedBytes, inOutMutableRanges, **mutableTypedBytes);
    if(!mutableRange)
        return MakeUnexpected(Failure{});

    const ECSRenderDetail::MaterialTypedInstanceRanges typedRanges{ *constantRange, *mutableRange };
    return ShadowOccluderMaterialContext{ ECSRenderDetail::BuildInstanceGpuData(transform, typedRanges), constantRange->byteOffset };
}

void RendererMaterialSystem::pruneMaterialInstanceMutableCache(){
    const u64 componentMutationVersion = m_world.componentMutationVersion<MaterialInstanceComponent>();
    if(componentMutationVersion == m_materialState.m_instanceMutableCacheComponentMutationVersion)
        return;

    m_materialState.m_instanceMutableCache.clear();
    m_materialState.m_instanceMutableCacheComponentMutationVersion = componentMutationVersion;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

