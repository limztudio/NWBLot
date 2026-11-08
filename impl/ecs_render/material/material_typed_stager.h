// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "material_system.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MaterialTypedStager final : NoCopy{
public:
    MaterialTypedStager(RendererMaterialSystem& system, Core::ECS::World& world, RendererResourceLookupMode::Enum lookupMode, MaterialTypedByteDataVector& bytes, usize capacity);


public:
    [[nodiscard]] Expected<MaterialTypedByteRange> constant(const MaterialSurfaceInfo& materialInfo);
    [[nodiscard]] Expected<MaterialTypedByteRange> mutableInstance(Core::ECS::EntityID entity, const MaterialSurfaceInfo& materialInfo);


private:
    [[nodiscard]] Expected<MaterialTypedByteRange> defaultMutable(const MaterialSurfaceInfo& materialInfo);
    using RangeMap = HashMap<MaterialTypedByteRangeKey, MaterialTypedByteRangeCache, Core::Alloc::ScratchArena, ECSRenderDetail::MaterialTypedByteRangeKeyHasher, EqualTo<MaterialTypedByteRangeKey>>;


private:
    RendererMaterialSystem& m_system;
    Core::ECS::World& m_world;
    RendererResourceLookupMode::Enum m_lookupMode;
    MaterialTypedByteDataVector& m_bytes;
    RangeMap m_ranges;
    ECSRenderDetail::MaterialTypedByteContentRangeMap m_mutableRanges;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

