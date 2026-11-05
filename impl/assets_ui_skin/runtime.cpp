// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset.h"

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
    usize cursor = 0u;
    const auto headerResult = Core::Assets::ReadMagicHeaderPayload<UiSkinHeaderBinary>(binary, s_UiSkinBinaryMagic, s_UiSkinLoadBinaryContext, s_UiSkinAssetKindLabel, cursor);
    if(!headerResult)
        return false;
    const UiSkinHeaderBinary& header = *headerResult;
    if(!ValidateUiSkinHeaderBinary(header, binary.size() - cursor, sizeof(UiSkinRegionBinary)))
        return false;

    UiSkin candidate(m_regions.get_allocator().arena(), virtualPath());
    candidate.m_texture.virtualPath = Name(header.textureNameHash);
    candidate.m_atlasWidth = header.atlasWidth;
    candidate.m_atlasHeight = header.atlasHeight;
    candidate.m_referenceDensity = header.referenceDensity;
    if(!ReadUiSkinRegionsPaletteAndTypography(binary, header.regionCount, cursor, candidate.m_regions, candidate.m_palette, candidate.m_typography))
        return false;
    if(!Core::Assets::ReadCompletePayload(binary, cursor, s_UiSkinLoadBinaryContext))
        return false;
    candidate.rebuildRegionIndex();
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
    rebuildRegionIndex();
    m_texture = texture;
    m_atlasWidth = width;
    m_atlasHeight = height;
    m_referenceDensity = referenceDensity;
    m_palette = {};
    m_typography = {};
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

