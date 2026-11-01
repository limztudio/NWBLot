// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset.h"

#include <impl/assets_texture/asset.h>

#include <core/assets/auto_registration.h>
#include <core/assets/binary_payload_io.h>
#include <core/common/log.h>

#include <global/simplemath.h>
#include <global/simd/vector_arithmetic.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_skin_runtime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_ASSET_CODEC_REGISTRAR(s_UiSkinAssetCodecAutoRegistrar, UiSkinAssetCodec);
NWB_DEFINE_SUBSYSTEM_ASSET_REGISTRAR(s_UiSubsystemAssetAutoRegistrar, UiSubsystem);
static_assert(UiSkinColorRole::kCount == s_UiSkinPaletteColorCount, "UI skin palette layout drifted");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static b8 IsValidRegion(const UiSkinRegion& region, const u32 atlasWidth, const u32 atlasHeight)noexcept{
    if(!region.name || region.name == Name(""))
        return false;
    if(!IsValidUiSkinRegionRectAndSlices(region.rectangle, region.sliceInsets, region.drawMode, atlasWidth, atlasHeight))
        return false;

    const UiSkinInsets& padding = region.padding;
    return
        IsFinite(padding.left) && padding.left >= 0.0f
        && IsFinite(padding.top) && padding.top >= 0.0f
        && IsFinite(padding.right) && padding.right >= 0.0f
        && IsFinite(padding.bottom) && padding.bottom >= 0.0f
        && VectorIsFinite(VectorAdd(VectorSet(padding.left, padding.top, 0.0f, 0.0f), VectorSet(padding.right, padding.bottom, 0.0f, 0.0f)), VectorComponentMask::s_XY)
        && IsFinite(region.minimumWidth) && region.minimumWidth >= 0.0f
        && IsFinite(region.minimumHeight) && region.minimumHeight >= 0.0f
    ;
}


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
    if(header.reserved0 != 0u || header.reserved1 != 0u){
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
        if(packed.reserved != 0u || packed.drawMode > static_cast<u32>(UiSkinDrawMode::NineSlice)){
            NWB_LOGGER_ERROR(NWB_TEXT("UiSkin::loadBinary failed: region {} contains an invalid draw mode or flags"), index);
            return false;
        }

        candidate.m_regions.push_back(UnpackUiSkinRegionBinary(packed));
    }
    UiSkinPalette palette;
    for(u32 index = 0u; index < UiSkinColorRole::kCount; ++index){
        const auto packedResult = ReadPOD<UiSkinColorBinary>(binary, cursor);
        if(!packedResult){
            NWB_LOGGER_ERROR(NWB_TEXT("UiSkin::loadBinary failed: malformed palette color {}"), index);
            return false;
        }
        palette.colors[index] = UnpackUiSkinColorBinary(*packedResult);
    }
    candidate.setPalette(palette);
    const auto typographyResult = ReadPOD<UiSkinTypographyBinary>(binary, cursor);
    if(!typographyResult){
        NWB_LOGGER_ERROR(NWB_TEXT("UiSkin::loadBinary failed: malformed typography payload"));
        return false;
    }
    candidate.setTypography(UnpackUiSkinTypographyBinary(*typographyResult));
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
        || !VectorIsFinite(VectorDivide(VectorSet(static_cast<f32>(m_atlasWidth), static_cast<f32>(m_atlasHeight), 0.0f, 0.0f), VectorReplicate(m_referenceDensity)), VectorComponentMask::s_XY)
        || m_regions.empty()
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("UiSkin::validatePayload failed: texture reference, atlas extent, reference density, or region count is invalid"));
        return false;
    }
    for(usize index = 0u; index < m_regions.size(); ++index){
        const UiSkinRegion& region = m_regions[index];
        if(!__hidden_ui_skin_runtime::IsValidRegion(region, m_atlasWidth, m_atlasHeight)){
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
        if(
            !IsFinite(color.r) || !IsValidUiSkinPaletteChannelValue(color.r, 0u)
            || !IsFinite(color.g) || !IsValidUiSkinPaletteChannelValue(color.g, 1u)
            || !IsFinite(color.b) || !IsValidUiSkinPaletteChannelValue(color.b, 2u)
            || !IsFinite(color.a) || !IsValidUiSkinPaletteChannelValue(color.a, s_UiSkinAlphaComponentIndex)
        ){
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

UiSkin::RegionVector UiSkin::takeRegions()noexcept{
    m_regionIndex.clear();
    return Move(m_regions);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

