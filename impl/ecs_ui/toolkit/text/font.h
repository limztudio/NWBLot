// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "types.h"
#include "baked_atlas.h"

#include <impl/assets_font/asset.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct FontSource{
    Core::Assets::AssetRef<Font> identity;
    const Font& font;
    u64 generation = 0u;
    const FontAtlas* atlas = nullptr;
};

struct GlyphCoverageBounds{
    Rect ink{};
    bool known = false;
};

struct RawShapedGlyph{
    u32 glyphId = 0u;
    u32 byteBegin = 0u;
    u32 byteEnd = 0u;
    Point offset;
    Point advance;
    Rect ink;
    GlyphCoverageBounds coverage{};
};

struct GlyphBitmap{
    PaintVector<u8> pixels;
    u32 width = 0u;
    u32 height = 0u;
    i32 bearingX = 0;
    i32 bearingY = 0;

    explicit GlyphBitmap(Core::Alloc::GlobalArena& arena)
        : pixels(arena)
    {}
};

class FontFaceState;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Native faces borrow copied immutable bytes. Native outline/raster calls belong to this version's owning UI thread.
class FontFace : NoCopy{
public:
    FontFace(Core::Alloc::GlobalArena& arena, const FontSource& source);
    ~FontFace();


public:
    [[nodiscard]] bool valid()const;
    [[nodiscard]] const Core::Assets::AssetRef<Font>& identity()const;
    [[nodiscard]] u64 generation()const;
    [[nodiscard]] u32 unitsPerEm()const;
    [[nodiscard]] bool coverageInkReliable()const;
    [[nodiscard]] const SharedBakedFontAtlas& bakedAtlas()const;
    [[nodiscard]] bool metrics(f32 fontSize, FontMetrics& output)const;
    [[nodiscard]] bool shape(
        const ShapeRequest& request,
        u32 byteBegin,
        u32 byteEnd,
        PaintVector<RawShapedGlyph>& output
    )const;
    [[nodiscard]] bool rasterize(u32 glyphId, u32 pixelSize, GlyphBitmap& output);


private:
    NotNullUniquePtr<FontFaceState, ArenaDeleter<FontFaceState, Core::Alloc::GlobalArena>> m_state;
};

using SharedFontFace = RefCountPtr<RefCounter<FontFace>, ArenaRefDeleter<RefCounter<FontFace>, Core::Alloc::GlobalArena>>;

[[nodiscard]] SharedFontFace MakeFontFace(Core::Alloc::GlobalArena& arena, const FontSource& source);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

