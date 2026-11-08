// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "material_typed_stager.h"

#include <core/ecs/world.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


MaterialTypedStager::MaterialTypedStager(RendererMaterialSystem& system, Core::ECS::World& world, const RendererResourceLookupMode::Enum lookupMode, MaterialTypedByteDataVector& bytes, const usize capacity)
    : m_system(system)
    , m_world(world)
    , m_lookupMode(lookupMode)
    , m_bytes(bytes)
    , m_ranges(0, ECSRenderDetail::MaterialTypedByteRangeKeyHasher(), EqualTo<MaterialTypedByteRangeKey>(), bytes.get_allocator().arena())
    , m_mutableRanges(0, ECSRenderDetail::MaterialTypedByteContentRangeMap::hasher(), ECSRenderDetail::MaterialTypedByteContentRangeMap::key_equal(), bytes.get_allocator().arena())
{
    m_ranges.reserve(capacity);
    m_mutableRanges.reserve(capacity);
}

Expected<MaterialTypedByteRange> MaterialTypedStager::constant(const MaterialSurfaceInfo& materialInfo){
    const MaterialTypedByteRangeKey rangeKey{ materialInfo.materialName, materialInfo.typedLayoutHash };
    auto cacheIt = m_ranges.try_emplace(rangeKey).first;
    MaterialTypedByteRangeCache& cache = cacheIt.value();
    if(cache.constantRangeCached)
        return cache.constantRange;
    const auto range = ECSRenderDetail::AppendMaterialTypedByteRange(materialInfo.constantTypedBytes, m_bytes);
    if(!range)
        return MakeUnexpected(Failure{});
    cache.constantRange = *range;
    cache.constantRangeCached = true;
    return range;
}

Expected<MaterialTypedByteRange> MaterialTypedStager::defaultMutable(const MaterialSurfaceInfo& materialInfo){
    const MaterialTypedByteRangeKey rangeKey{ materialInfo.materialName, materialInfo.typedLayoutHash };
    auto cacheIt = m_ranges.try_emplace(rangeKey).first;
    MaterialTypedByteRangeCache& cache = cacheIt.value();
    if(cache.defaultMutableRangeCached)
        return cache.defaultMutableRange;
    const auto range = ECSRenderDetail::FindOrAppendMaterialTypedByteRange(materialInfo.mutableDefaultTypedBytes, m_bytes, m_mutableRanges);
    if(!range)
        return MakeUnexpected(Failure{});
    cache.defaultMutableRange = *range;
    cache.defaultMutableRangeCached = true;
    return range;
}

Expected<MaterialTypedByteRange> MaterialTypedStager::mutableInstance(const Core::ECS::EntityID entity, const MaterialSurfaceInfo& materialInfo){
    const MaterialInstanceComponent* materialInstance = m_world.tryGetComponent<MaterialInstanceComponent>(entity);
    if(!materialInstance || materialInstance->overrides.empty())
        return defaultMutable(materialInfo);
    const auto mutableTypedBytes = m_lookupMode == RendererResourceLookupMode::CreateMissing
        ? m_system.prepareMaterialInstanceMutableTypedBytes(entity, materialInfo, materialInstance)
        : m_system.findPreparedMaterialInstanceMutableTypedBytes(entity, materialInfo, materialInstance)
    ;
    if(!mutableTypedBytes)
        return MakeUnexpected(Failure{});
    return ECSRenderDetail::FindOrAppendMaterialTypedByteRange(*mutableTypedBytes, m_bytes, m_mutableRanges);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

