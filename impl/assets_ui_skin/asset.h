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
    Name name = NAME_NONE;
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
    Array<UiSkinColor, UiSkinColorRole::Count> colors{};
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
    {}
    UiSkin(Core::Assets::AssetArena& arena, const Name& virtualPath)
        : Core::Assets::TypedAsset<UiSkin>(virtualPath)
        , m_regions(arena)
    {}


public:
    bool loadBinary(const Core::Assets::AssetBytes& binary);
    [[nodiscard]] bool validatePayload()const;
    [[nodiscard]] bool validateTexture(const Texture& texture)const;

    void setAtlas(Core::Assets::AssetRef<Texture> texture, u32 width, u32 height, f32 referenceDensity, RegionVector&& regions);
    void setPalette(const UiSkinPalette& palette){ m_palette = palette; m_hasPalette = true; }


public:
    [[nodiscard]] const Core::Assets::AssetRef<Texture>& texture()const{ return m_texture; }
    [[nodiscard]] u32 atlasWidth()const{ return m_atlasWidth; }
    [[nodiscard]] u32 atlasHeight()const{ return m_atlasHeight; }
    [[nodiscard]] f32 referenceDensity()const{ return m_referenceDensity; }
    [[nodiscard]] const RegionVector& regions()const{ return m_regions; }
    [[nodiscard]] bool hasPalette()const{ return m_hasPalette; }
    [[nodiscard]] const UiSkinPalette& palette()const{ return m_palette; }
    [[nodiscard]] const UiSkinRegion* findRegion(const Name& name)const;


private:
    RegionVector m_regions;
    Core::Assets::AssetRef<Texture> m_texture;
    u32 m_atlasWidth = 0u;
    u32 m_atlasHeight = 0u;
    f32 m_referenceDensity = 1.0f;
    UiSkinPalette m_palette;
    bool m_hasPalette = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_ASSET_CODEC(UiSkinAssetCodec, UiSkin);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

