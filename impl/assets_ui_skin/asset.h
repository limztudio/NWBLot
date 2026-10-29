// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"
#include <impl/assets_ui_skin/types.adhd.h>

#include <core/assets/module.h>
#include <core/assets/ref.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Texture;

struct UiSkinPalette{
    Array<UiSkinColor, UiSkinColorRole::kCount> colors{{
        s_DefaultTextNormalColor, // TextNormal
        s_DefaultTextDisabledColor, // TextDisabled
        s_DefaultTextTooltipColor, // TextTooltip
        s_DefaultEditBackgroundColor, // EditBackground
        s_DefaultEditSelectionColor, // EditSelection
        s_DefaultEditInactiveSelectionColor, // EditInactiveSelection
        s_DefaultEditCaretColor, // EditCaret
        s_DefaultEditPreeditColor, // EditPreedit
        s_DefaultScrollbarTrackColor, // ScrollbarTrack
        s_DefaultScrollbarThumbColor, // ScrollbarThumb
        s_DefaultScrollbarDisabledColor, // ScrollbarDisabled
        s_DefaultPopupBackdropColor, // PopupBackdrop
        s_DefaultControlHoverTintColor, // ControlHoverTint
        s_DefaultControlPressedTintColor, // ControlPressedTint
        s_DefaultControlDisabledTintColor, // ControlDisabledTint
        s_DefaultProgressTrackTintColor, // ProgressTrackTint
        s_DefaultProgressFillTintColor, // ProgressFillTint
    }};
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiSkin final : public Core::Assets::TypedAsset<UiSkin>{
public:
    using RegionVector = Core::Assets::AssetVector<UiSkinRegion>;


public:
    NWB_DEFINE_ASSET_TYPE("ui_skin")


public:
    explicit UiSkin(Core::Assets::AssetArena& arena)noexcept
        : m_regions(arena)
        , m_regionIndex(arena)
    {}
    UiSkin(Core::Assets::AssetArena& arena, const Name& virtualPath)noexcept
        : Core::Assets::TypedAsset<UiSkin>(virtualPath)
        , m_regions(arena)
        , m_regionIndex(arena)
    {}


public:
    b8 loadBinary(Span<const u8> binary);
    [[nodiscard]] b8 validatePayload()const;
    [[nodiscard]] b8 validateTexture(const Texture& texture)const;

    void setAtlas(Core::Assets::AssetRef<Texture> texture, u32 width, u32 height, f32 referenceDensity, RegionVector&& regions);
    void setPalette(const UiSkinPalette& palette)noexcept{ m_palette = palette; }

    void setTypography(const UiSkinTypography& typography)noexcept{ m_typography = typography; }


public:
    [[nodiscard]] const Core::Assets::AssetRef<Texture>& texture()const noexcept{ return m_texture; }

    [[nodiscard]] u32 atlasWidth()const noexcept{ return m_atlasWidth; }

    [[nodiscard]] u32 atlasHeight()const noexcept{ return m_atlasHeight; }

    [[nodiscard]] f32 referenceDensity()const noexcept{ return m_referenceDensity; }

    [[nodiscard]] const RegionVector& regions()const noexcept{ return m_regions; }

    [[nodiscard]] const UiSkinPalette& palette()const noexcept{ return m_palette; }

    [[nodiscard]] const UiSkinTypography& typography()const noexcept{ return m_typography; }
    [[nodiscard]] const UiSkinRegion* findRegion(const Name& name)const noexcept;


private:
    void rebuildRegionIndex();


private:
    RegionVector m_regions;
    Core::Assets::AssetVector<u32> m_regionIndex;
    Core::Assets::AssetRef<Texture> m_texture;
    u32 m_atlasWidth = 0u;
    u32 m_atlasHeight = 0u;
    f32 m_referenceDensity = 1.0f;
    UiSkinPalette m_palette;
    UiSkinTypography m_typography;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_ASSET_CODEC(UiSkinAssetCodec, UiSkin);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

