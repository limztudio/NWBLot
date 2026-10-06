// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <core/assets/module.h>
#include <core/assets/ref.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Texture;

inline constexpr u32 s_UiSkinMaxRegionCount = 4096u;

namespace UiSkinDrawMode{
    enum Enum : u8{
        Sprite = 0u,
        NineSlice,
    };
};

// Atlas rectangles and slice borders use top-left-origin pixels; padding and minimum sizes use logical units.
struct UiSkinRect{
    u32 x = 0u;
    u32 y = 0u;
    u32 width = 0u;
    u32 height = 0u;
};

struct UiSkinSliceInsets{
    u32 left = 0u;
    u32 top = 0u;
    u32 right = 0u;
    u32 bottom = 0u;
};

struct UiSkinInsets{
    f32 left = 0.0f;
    f32 top = 0.0f;
    f32 right = 0.0f;
    f32 bottom = 0.0f;
};

struct UiSkinRegion{
    Name name = s_NameNone;
    UiSkinRect rectangle;
    UiSkinSliceInsets sliceInsets;
    UiSkinInsets padding;
    f32 minimumWidth = 0.0f;
    f32 minimumHeight = 0.0f;
    UiSkinDrawMode::Enum drawMode = UiSkinDrawMode::Sprite;
};

namespace UiSkinColorRole{
    enum Enum : u8{
        TextNormal,
        TextDisabled,
        TextTooltip,
        EditBackground,
        EditSelection,
        EditInactiveSelection,
        EditCaret,
        EditPreedit,
        ScrollbarTrack,
        ScrollbarThumb,
        ScrollbarDisabled,
        PopupBackdrop,
        ControlHoverTint,
        ControlPressedTint,
        ControlDisabledTint,
        ProgressTrackTint,
        ProgressFillTint,
        Count,
    };
};

struct UiSkinColor{
    f32 r = 0.0f;
    f32 g = 0.0f;
    f32 b = 0.0f;
    f32 a = 0.0f;
};

struct UiSkinPalette{
    Array<UiSkinColor, UiSkinColorRole::Count> colors{{
        { 0.92f, 0.94f, 0.98f, 1.0f }, // TextNormal
        { 0.48f, 0.5f, 0.55f, 1.0f }, // TextDisabled
        { 1.0f, 1.0f, 1.0f, 1.0f }, // TextTooltip
        { 0.08f, 0.1f, 0.14f, 1.0f }, // EditBackground
        { 0.2f, 0.4f, 0.78f, 0.75f }, // EditSelection
        { 0.28f, 0.31f, 0.38f, 0.55f }, // EditInactiveSelection
        { 0.95f, 0.97f, 1.0f, 1.0f }, // EditCaret
        { 0.5f, 0.72f, 1.0f, 1.0f }, // EditPreedit
        { 0.08f, 0.1f, 0.14f, 1.0f }, // ScrollbarTrack
        { 0.35f, 0.4f, 0.48f, 1.0f }, // ScrollbarThumb
        { 0.25f, 0.28f, 0.32f, 1.0f }, // ScrollbarDisabled
        { 0.0f, 0.0f, 0.0f, 0.4f }, // PopupBackdrop
        { 1.08f, 1.08f, 1.08f, 1.0f }, // ControlHoverTint
        { 0.85f, 0.85f, 0.85f, 1.0f }, // ControlPressedTint
        { 0.55f, 0.55f, 0.55f, 0.6f }, // ControlDisabledTint
        { 1.0f, 1.0f, 1.0f, 1.0f }, // ProgressTrackTint
        { 1.0f, 1.0f, 1.0f, 1.0f }, // ProgressFillTint
    }};
};

struct UiSkinTypography{
    f32 defaultFontSize = 16.0f;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiSkin final : public Core::Assets::TypedAsset<UiSkin>{
public:
    using RegionVector = Core::Assets::AssetVector<UiSkinRegion>;


public:
    NWB_DEFINE_ASSET_TYPE("ui_skin")


public:
    explicit UiSkin(Core::Assets::AssetArena& arena)
        : m_regions(arena)
        , m_regionIndex(arena)
    {}
    UiSkin(Core::Assets::AssetArena& arena, const Name& virtualPath)
        : Core::Assets::TypedAsset<UiSkin>(virtualPath)
        , m_regions(arena)
        , m_regionIndex(arena)
    {}


public:
    bool loadBinary(const Core::Assets::AssetBytes& binary);
    [[nodiscard]] bool validatePayload()const;
    [[nodiscard]] bool validateTexture(const Texture& texture)const;

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

