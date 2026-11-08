// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset.h"

#include <impl/assets_ui_skin/validation.adhd.h>
#include <impl/assets_ui_skin/ui_skin_binary_codec.adhd.h>
#include <impl/assets_texture/asset.h>

#include <core/assets/auto_registration.h>
#include <core/assets/binary_payload_io.h>
#include <core/common/log.h>

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_skin_runtime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_ASSET_CODEC_REGISTRAR(s_UiSkinAssetCodecAutoRegistrar, UiSkinAssetCodec);
NWB_DEFINE_SUBSYSTEM_ASSET_REGISTRAR(s_UiSubsystemAssetAutoRegistrar, UiSubsystem);
static_assert(UiSkinColorRole::kCount == s_UiSkinPaletteColorCount, "UI skin palette layout drifted");
static_assert(sizeof(UiSkinHeaderBinary) == s_UiSkinHeaderBinaryBytes, "UiSkinHeaderBinary layout drifted");
static_assert(sizeof(UiSkinRegionBinary) == s_UiSkinRegionBinaryBytes, "UiSkinRegionBinary layout drifted");
static_assert(sizeof(UiSkinColor) == s_UiSkinColorBytes, "UiSkinColor layout drifted");
static_assert(sizeof(UiSkinTypography) == s_UiSkinTypographyBytes, "UiSkinTypography layout drifted");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 UiSkin::loadBinary(const Span<const u8> binary){
    UiSkin candidate(m_regions.get_allocator().arena(), virtualPath());
    if(!UiSkinBinaryCodec().deserialize(
        binary,
        candidate.m_texture,
        candidate.m_atlasWidth,
        candidate.m_atlasHeight,
        candidate.m_referenceDensity,
        candidate.m_regions,
        candidate.m_regionIndex,
        candidate.m_palette,
        candidate.m_typography
    ))
        return false;
    if(!candidate.validatePayload())
        return false;

    *this = Move(candidate);
    return true;
}

b8 UiSkin::validatePayload()const{
    if(!checkVirtualPath(NWB_TEXT("UiSkin::validatePayload")))
        return false;
    return ValidateUiSkinPayloadData(
        m_texture,
        m_atlasWidth,
        m_atlasHeight,
        m_referenceDensity,
        Span<const UiSkinRegion>(m_regions),
        Span<const u32>(m_regionIndex),
        m_palette,
        m_typography
    );
}

b8 UiSkin::validateTexture(const Texture& texture)const{
    if(!validatePayload())
        return false;
    return ValidateUiSkinAtlasTextureParameters(
        texture.virtualPath(),
        m_texture.name(),
        texture.dimension() == TextureDimension::Texture2D,
        texture.depth(),
        texture.width(),
        texture.height(),
        m_atlasWidth,
        m_atlasHeight
    );
}

void UiSkin::setAtlas(Core::Assets::AssetRef<Texture> texture, const u32 width, const u32 height, const f32 referenceDensity, RegionVector&& regions){
    RegionVector candidate(m_regions.get_allocator().arena());
    candidate = Move(regions);
    m_regions = Move(candidate);
    ResetUiSkinAtlasFields(
        texture,
        width,
        height,
        referenceDensity,
        m_regions,
        m_regionIndex,
        m_texture,
        m_atlasWidth,
        m_atlasHeight,
        m_referenceDensity,
        m_palette,
        m_typography
    );
}

const UiSkinRegion* UiSkin::findRegion(const Name& name)const noexcept{
    const auto regionPosition = TryFindUiSkinRegionIndex(Span<const UiSkinRegion>(m_regions), Span<const u32>(m_regionIndex), name);
    if(!regionPosition)
        return nullptr;
    return &m_regions[*regionPosition];
}

void UiSkin::rebuildRegionIndex(){
    RebuildUiSkinRegionIndex(m_regions, m_regionIndex);
}

b8 ValidateUiSkinToolkitContract(const UiSkin& skin){
    if(!skin.validatePayload())
        return false;
    return ValidateUiSkinToolkitRegions(Span<const UiSkinRegion>(skin.regions()), Span<const u32>(skin.regionIndex()));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

