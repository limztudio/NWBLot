// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "material_instance_mutable_cache.h"

#include <impl/ecs_render/material/instance_typed_ranges.adhd.h>
#include <impl/ecs_render/material/material_instance_override_applier.adhd.h>
#include <impl/ecs_render/material/material_surface_builder.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Span<const u8>> MaterialInstanceMutableCache::prepare(
    Core::ECS::World& world,
    const Core::ECS::EntityID entity,
    const MaterialSurfaceInfo& materialInfo,
    const MaterialInstanceComponent* materialInstance
){
    prune(world);

    if(!materialInstance || materialInstance->overrides.empty())
        return Span<const u8>(materialInfo.mutableDefaultTypedBytes);

    auto it = m_entries.try_emplace(entity, m_arena).first;
    MaterialInstanceMutableCacheEntry& cacheEntry = it.value();
    if(::NWB::Impl::MaterialInstanceMutableCacheEntryMatches(
        cacheEntry.materialName,
        cacheEntry.materialInterface,
        cacheEntry.typedLayoutHash,
        cacheEntry.revision,
        materialInfo.materialName,
        materialInfo.materialInterface,
        materialInfo.typedLayoutHash,
        *materialInstance
    ))
        return Span<const u8>(cacheEntry.mutableTypedBytes);

    MaterialSurfaceBuilder().copyTypedBytes(materialInfo.mutableDefaultTypedBytes, cacheEntry.mutableTypedBytes);
    Span<u8> mutableBytes(cacheEntry.mutableTypedBytes);
    if(!MaterialInstanceOverrideApplier().apply(entity.id, materialInfo.materialName, materialInfo.materialInterface, materialInfo.typedLayoutBlocks, materialInfo.typedLayoutFields, *materialInstance, mutableBytes)){
        m_entries.erase(it);
        return MakeUnexpected(Failure{});
    }

    cacheEntry.materialName = materialInfo.materialName;
    cacheEntry.materialInterface = materialInfo.materialInterface;
    cacheEntry.typedLayoutHash = materialInfo.typedLayoutHash;
    cacheEntry.revision = materialInstance->revision;

    return Span<const u8>(cacheEntry.mutableTypedBytes);
}

Expected<Span<const u8>> MaterialInstanceMutableCache::find(const Core::ECS::EntityID entity, const MaterialSurfaceInfo& materialInfo, const MaterialInstanceComponent* materialInstance)const noexcept{
    if(!materialInstance || materialInstance->overrides.empty())
        return Span<const u8>(materialInfo.mutableDefaultTypedBytes);

    const auto found = m_entries.find(entity);
    if(found == m_entries.end())
        return MakeUnexpected(Failure{});

    const MaterialInstanceMutableCacheEntry& cacheEntry = found.value();
    if(!::NWB::Impl::MaterialInstanceMutableCacheEntryMatches(
        cacheEntry.materialName,
        cacheEntry.materialInterface,
        cacheEntry.typedLayoutHash,
        cacheEntry.revision,
        materialInfo.materialName,
        materialInfo.materialInterface,
        materialInfo.typedLayoutHash,
        *materialInstance
    ))
        return MakeUnexpected(Failure{});

    return Span<const u8>(cacheEntry.mutableTypedBytes);
}

void MaterialInstanceMutableCache::prune(Core::ECS::World& world)noexcept{
    const u64 componentMutationVersion = world.componentMutationVersion<MaterialInstanceComponent>();
    if(componentMutationVersion == m_componentMutationVersion)
        return;

    m_entries.clear();
    m_componentMutationVersion = componentMutationVersion;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
