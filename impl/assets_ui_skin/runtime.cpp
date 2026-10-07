// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset.h"
#include "binary_payload.h"

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
static_assert(UiSkinColorRole::Count == UiSkinBinaryPayload::s_UiSkinPaletteColorCount, "UI skin palette layout drifted");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool IsValidRegion(const UiSkinRegion& region, const u32 atlasWidth, const u32 atlasHeight)noexcept{
    if(!region.name || region.name == Name(""))
        return false;
    const UiSkinRect& rectangle = region.rectangle;
    if(
        rectangle.width == 0u
        || rectangle.height == 0u
        || rectangle.x > atlasWidth
        || rectangle.y > atlasHeight
        || rectangle.width > atlasWidth - rectangle.x
        || rectangle.height > atlasHeight - rectangle.y
    )
        return false;

    const UiSkinSliceInsets& slice = region.sliceInsets;
    if(region.drawMode == UiSkinDrawMode::Sprite){
        if(slice.left != 0u || slice.top != 0u || slice.right != 0u || slice.bottom != 0u)
            return false;
    }
    else if(region.drawMode == UiSkinDrawMode::NineSlice){
        if(static_cast<u64>(slice.left) + slice.right > rectangle.width || static_cast<u64>(slice.top) + slice.bottom > rectangle.height)
            return false;
    }
    else
        return false;

    const UiSkinInsets& padding = region.padding;
    return
        IsFinite(padding.left) && padding.left >= 0.0f
        && IsFinite(padding.top) && padding.top >= 0.0f
        && IsFinite(padding.right) && padding.right >= 0.0f
        && IsFinite(padding.bottom) && padding.bottom >= 0.0f
        && IsFinite(padding.left + padding.right)
        && IsFinite(padding.top + padding.bottom)
        && IsFinite(region.minimumWidth) && region.minimumWidth >= 0.0f
        && IsFinite(region.minimumHeight) && region.minimumHeight >= 0.0f
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool UiSkin::loadBinary(const Core::Assets::AssetBytes& binary){
    usize cursor = 0u;
    UiSkinBinaryPayload::HeaderBinary header;
    if(!Core::Assets::ReadMagicHeaderPayload(
        binary,
        cursor,
        header,
        UiSkinBinaryPayload::s_UiSkinMagic,
        NWB_TEXT("UiSkin::loadBinary"),
        NWB_TEXT("UI skin")
    ))
        return false;
    if(header.version != UiSkinBinaryPayload::s_UiSkinVersion){
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
    if(header.regionCount == 0u || header.regionCount > (binary.size() - cursor) / sizeof(UiSkinBinaryPayload::RegionBinary)){
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
        UiSkinBinaryPayload::RegionBinary packed;
        if(!ReadPOD(binary, cursor, packed)){
            NWB_LOGGER_ERROR(NWB_TEXT("UiSkin::loadBinary failed: malformed region {}"), index);
            return false;
        }
        if(packed.reserved != 0u || packed.drawMode > static_cast<u32>(UiSkinDrawMode::NineSlice)){
            NWB_LOGGER_ERROR(NWB_TEXT("UiSkin::loadBinary failed: region {} contains an invalid draw mode or flags"), index);
            return false;
        }

        UiSkinRegion region;
        region.name = Name(packed.nameHash);
        region.rectangle = { packed.x, packed.y, packed.width, packed.height };
        region.sliceInsets = { packed.sliceLeft, packed.sliceTop, packed.sliceRight, packed.sliceBottom };
        region.padding = { packed.paddingLeft, packed.paddingTop, packed.paddingRight, packed.paddingBottom };
        region.minimumWidth = packed.minimumWidth;
        region.minimumHeight = packed.minimumHeight;
        region.drawMode = static_cast<UiSkinDrawMode::Enum>(packed.drawMode);
        candidate.m_regions.push_back(region);
    }
    UiSkinPalette palette;
    for(u32 index = 0u; index < UiSkinColorRole::Count; ++index){
        UiSkinBinaryPayload::ColorBinary packed;
        if(!ReadPOD(binary, cursor, packed)){
            NWB_LOGGER_ERROR(NWB_TEXT("UiSkin::loadBinary failed: malformed palette color {}"), index);
            return false;
        }
        palette.colors[index] = { packed.r, packed.g, packed.b, packed.a };
    }
    candidate.setPalette(palette);
    UiSkinBinaryPayload::TypographyBinary typography;
    if(!ReadPOD(binary, cursor, typography)){
        NWB_LOGGER_ERROR(NWB_TEXT("UiSkin::loadBinary failed: malformed typography payload"));
        return false;
    }
    candidate.setTypography({ typography.defaultFontSize });
    if(!Core::Assets::ReadCompletePayload(binary, cursor, NWB_TEXT("UiSkin::loadBinary")))
        return false;
    candidate.rebuildRegionIndex();
    if(!candidate.validatePayload())
        return false;

    *this = Move(candidate);
    return true;
}

bool UiSkin::validatePayload()const{
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
        || !IsFinite(static_cast<f32>(m_atlasWidth) / m_referenceDensity)
        || !IsFinite(static_cast<f32>(m_atlasHeight) / m_referenceDensity)
        || m_regions.empty()
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("UiSkin::validatePayload failed: texture reference, atlas extent, reference density, or region count is invalid"));
        return false;
    }
    for(usize index = 0u; index < m_regions.size(); ++index){
        const UiSkinRegion& region = m_regions[index];
        if(!__hidden_ui_skin_runtime::IsValidRegion(region, m_atlasWidth, m_atlasHeight)){
            NWB_LOGGER_ERROR(NWB_TEXT("UiSkin::validatePayload failed: region {} '{}' has invalid bounds, slice borders, draw mode, or logical metrics")
                , index
                , StringConvert(region.name.resolvedText())
            );
            return false;
        }
    }
    for(usize index = 1u; index < m_regionIndex.size(); ++index){
        const Name& name = m_regions[m_regionIndex[index]].name;
        if(name == m_regions[m_regionIndex[index - 1u]].name){
            NWB_LOGGER_ERROR(NWB_TEXT("UiSkin::validatePayload failed: duplicate region '{}'"), StringConvert(name.resolvedText()));
            return false;
        }
    }
    for(usize index = 0u; index < m_palette.colors.size(); ++index){
        const UiSkinColor& color = m_palette.colors[index];
        if(
            !IsFinite(color.r) || color.r < 0.0f || color.r > 16.0f
            || !IsFinite(color.g) || color.g < 0.0f || color.g > 16.0f
            || !IsFinite(color.b) || color.b < 0.0f || color.b > 16.0f
            || !IsFinite(color.a) || color.a < 0.0f || color.a > 1.0f
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("UiSkin::validatePayload failed: palette color {} needs finite RGB in [0, 16] and alpha in [0, 1]"), index);
            return false;
        }
    }
    if(!IsFinite(m_typography.defaultFontSize)
        || m_typography.defaultFontSize < 1.0f / 64.0f || m_typography.defaultFontSize > 2048.0f){
        NWB_LOGGER_ERROR(NWB_TEXT("UiSkin::validatePayload failed: typography needs a default font size in [1/64, 2048]"));
        return false;
    }
    return true;
}

bool UiSkin::validateTexture(const Texture& texture)const{
    if(!validatePayload())
        return false;
    if(
        texture.virtualPath() != m_texture.name()
        || texture.dimension() != TextureDimension::Texture2D || texture.depth() != 1u
        || texture.width() != m_atlasWidth || texture.height() != m_atlasHeight
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("UiSkin::validateTexture failed: texture identity, 2D dimension, or extent does not match the atlas"));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

