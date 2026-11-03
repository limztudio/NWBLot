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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 UiSkin::loadBinary(const Span<const u8> binary){
    usize cursor = 0u;
    const auto headerResult = Core::Assets::ReadMagicHeaderPayload<UiSkinHeaderBinary>(binary, s_UiSkinBinaryMagic, s_UiSkinLoadBinaryContext, s_UiSkinAssetKindLabel, cursor);
    if(!headerResult)
        return false;
    const UiSkinHeaderBinary& header = *headerResult;
    if(header.version != s_UiSkinBinaryVersion){
        NWB_LOGGER_ERROR(NWB_TEXT("UiSkin::loadBinary failed: unsupported UI skin version {}; recook required"), header.version);
        return false;
    }
    if(!IsValidUiSkinHeaderFlags(header)){
        NWB_LOGGER_ERROR(NWB_TEXT("UiSkin::loadBinary failed: header contains unsupported flags"));
        return false;
    }
    if(header.regionCount > s_UiSkinMaxRegionCount){
        NWB_LOGGER_ERROR(NWB_TEXT("UiSkin::loadBinary failed: region count {} exceeds schema limit {}"), header.regionCount, s_UiSkinMaxRegionCount);
        return false;
    }
    if(header.regionCount == 0u || header.regionCount > (binary.size() - cursor) / sizeof(UiSkinRegionBinary)){
        NWB_LOGGER_ERROR(NWB_TEXT("UiSkin::loadBinary failed: region count is empty or exceeds the payload"));
        return false;
    }

    UiSkin candidate(m_regions.get_allocator().arena(), virtualPath());
    candidate.m_texture.virtualPath = Name(header.textureNameHash);
    candidate.m_atlasWidth = header.atlasWidth;
    candidate.m_atlasHeight = header.atlasHeight;
    candidate.m_referenceDensity = header.referenceDensity;
    candidate.m_regions.reserve(header.regionCount);
    for(u32 index = 0u; index < header.regionCount; ++index){
        const auto packedResult = ReadPOD<UiSkinRegionBinary>(binary, cursor);
        if(!packedResult){
            NWB_LOGGER_ERROR(NWB_TEXT("UiSkin::loadBinary failed: malformed region {}"), index);
            return false;
        }
        const UiSkinRegionBinary& packed = *packedResult;
        if(!IsValidUiSkinRegionBinaryFlags(packed)){
            NWB_LOGGER_ERROR(NWB_TEXT("UiSkin::loadBinary failed: region {} contains an invalid draw mode or flags"), index);
            return false;
        }

        candidate.m_regions.push_back(UnpackUiSkinRegionBinary(packed));
    }
    UiSkinPalette palette;
    for(u32 index = 0u; index < UiSkinColorRole::kCount; ++index){
        const auto packedResult = ReadPOD<UiSkinColor>(binary, cursor);
        if(!packedResult){
            NWB_LOGGER_ERROR(NWB_TEXT("UiSkin::loadBinary failed: malformed palette color {}"), index);
            return false;
        }
        palette.colors[index] = *packedResult;
    }
    candidate.setPalette(palette);
    const auto typographyResult = ReadPOD<UiSkinTypography>(binary, cursor);
    if(!typographyResult){
        NWB_LOGGER_ERROR(NWB_TEXT("UiSkin::loadBinary failed: malformed typography payload"));
        return false;
    }
    candidate.setTypography(*typographyResult);
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
    if(m_regions.size() > s_UiSkinMaxRegionCount){
        NWB_LOGGER_ERROR(NWB_TEXT("UiSkin::validatePayload failed: region count {} exceeds schema limit {}"), m_regions.size(), s_UiSkinMaxRegionCount);
        return false;
    }
    if(
        !m_texture.valid() || m_texture.name() == Name("")
        || m_atlasWidth == 0u || m_atlasHeight == 0u
        || !IsFinite(m_referenceDensity) || m_referenceDensity <= 0.0f
        || !(IsFinite(static_cast<f32>(m_atlasWidth) / m_referenceDensity) && IsFinite(static_cast<f32>(m_atlasHeight) / m_referenceDensity))
        || m_regions.empty()
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("UiSkin::validatePayload failed: texture reference, atlas extent, reference density, or region count is invalid"));
        return false;
    }
    for(usize index = 0u; index < m_regions.size(); ++index){
        const UiSkinRegion& region = m_regions[index];
        if(!IsValidUiSkinRegion(region, m_atlasWidth, m_atlasHeight)){
            NWB_LOGGER_ERROR(NWB_TEXT("UiSkin::validatePayload failed: region {} '{}' has invalid bounds, slice borders, draw mode, or logical metrics"), index, ConvertString(region.name.resolvedText()));
            return false;
        }
    }
    for(usize index = 1u; index < m_regionIndex.size(); ++index){
        const Name& name = m_regions[m_regionIndex[index]].name;
        if(name == m_regions[m_regionIndex[index - 1u]].name){
            NWB_LOGGER_ERROR(NWB_TEXT("UiSkin::validatePayload failed: duplicate region '{}'"), ConvertString(name.resolvedText()));
            return false;
        }
    }
    for(usize index = 0u; index < m_palette.colors.size(); ++index){
        const UiSkinColor& color = m_palette.colors[index];
        if(!IsValidUiSkinPaletteColor(color)){
            NWB_LOGGER_ERROR(NWB_TEXT("UiSkin::validatePayload failed: palette color {} needs finite RGB in [0, 16] and alpha in [0, 1]"), index);
            return false;
        }
    }
    if(!IsFinite(m_typography.defaultFontSize) || !IsValidUiSkinDefaultFontSize(m_typography.defaultFontSize)){
        NWB_LOGGER_ERROR(NWB_TEXT("UiSkin::validatePayload failed: typography needs a default font size in [1/64, 2048]"));
        return false;
    }
    return true;
}

b8 UiSkin::validateTexture(const Texture& texture)const{
    if(!validatePayload())
        return false;
    if(texture.virtualPath() != m_texture.name() || texture.dimension() != TextureDimension::Texture2D || texture.depth() != 1u || texture.width() != m_atlasWidth || texture.height() != m_atlasHeight){
        NWB_LOGGER_ERROR(NWB_TEXT("UiSkin::validateTexture failed: texture identity, 2D dimension, or extent does not match the atlas"));
        return false;
    }
    return true;
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

b8 ValidateUiSkinToolkitContract(const UiSkin& skin){
    if(!skin.validatePayload())
        return false;
    for(usize index = 0u; index < s_UiSkinRequiredRegionCount; ++index){
        const UiSkinRequiredRegion required = ResolveUiSkinRequiredRegion(index);
        if(!skin.hasRegion(required.name)){
            NWB_LOGGER_ERROR(NWB_TEXT("UI skin toolkit contract 'widgets' failed: missing required region '{}'"), ConvertString(required.text));
            return false;
        }
    }
    if(!skin.hasRegion(s_WindowResizeRegionName) && !skin.hasRegion(s_WhiteRegionName)){
        NWB_LOGGER_ERROR(NWB_TEXT("UI skin toolkit contract 'widgets' failed: window resizing requires 'window.resize' or 'white'"));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

