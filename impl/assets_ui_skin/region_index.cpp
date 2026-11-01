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
    // Small skins scan faster; oversized programmatic objects remain inspectable without an unbounded index.
    if(m_regions.size() <= s_UiSkinLinearLookupRegionLimit || m_regions.size() > s_UiSkinMaxRegionCount){
        for(const UiSkinRegion& region : m_regions){
            if(region.name == name)
                return &region;
        }
        return nullptr;
    }
    usize first = 0u;
    usize count = m_regionIndex.size();
    while(count > 0u){
        const usize step = count / 2u;
        const usize middle = first + step;
        if(m_regions[m_regionIndex[middle]].name < name){
            first = middle + 1u;
            count -= step + 1u;
        }
        else
            count = step;
    }
    if(first == m_regionIndex.size())
        return nullptr;
    const UiSkinRegion& region = m_regions[m_regionIndex[first]];
    return region.name == name ? &region : nullptr;
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

