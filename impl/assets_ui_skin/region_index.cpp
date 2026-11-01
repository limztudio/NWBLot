// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void UiSkin::setAtlas(Core::Assets::AssetRef<Texture> texture, const u32 width, const u32 height, const f32 referenceDensity, RegionVector&& regions){
    RegionVector candidate(m_regions.get_allocator().arena());
    candidate = Move(regions);
    m_regions = Move(candidate);
    rebuildRegionIndex();
    m_texture = texture;
    m_atlasWidth = width;
    m_atlasHeight = height;
    m_referenceDensity = referenceDensity;
    m_palette = {};
    m_typography = {};
}

const UiSkinRegion* UiSkin::findRegion(const Name& name)const noexcept{
    usize regionPosition = 0u;
    if(!TryFindUiSkinRegionIndex(Span<const UiSkinRegion>(m_regions), Span<const u32>(m_regionIndex), name, regionPosition))
        return nullptr;
    return &m_regions[regionPosition];
}

void UiSkin::rebuildRegionIndex(){
    Core::Assets::AssetVector<u32> index(m_regions.get_allocator().arena());
    if(m_regions.size() <= s_UiSkinMaxRegionCount){
        index.reserve(m_regions.size());
        for(u32 position = 0u; position < m_regions.size(); ++position)
            index.push_back(position);
        const Span<const UiSkinRegion> regions = m_regions;
        Sort(index.begin(), index.end(), [&regions](const u32 left, const u32 right)noexcept{
            const Name& a = regions[left].name;
            const Name& b = regions[right].name;
            return a == b ? left < right : a < b;
        });
    }
    m_regionIndex = Move(index);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

