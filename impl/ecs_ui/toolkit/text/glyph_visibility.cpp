// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "glyph_visibility.h"

#include "atlas.h"

#include <impl/ecs_ui/toolkit/layout/validation.h>

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_glyph_visibility{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ValidLocation(const PlacedGlyph& glyph, const Point& topLeft){
    return IsFinite(glyph.position.x) && IsFinite(glyph.position.y) && IsFinite(topLeft.x) && IsFinite(topLeft.y);
}

[[nodiscard]] static bool ValidFontSize(const f32 fontSize){
    return IsFinite(fontSize) && fontSize >= 1.0f / 64.0f && fontSize <= 2048.0f;
}

[[nodiscard]] static bool MakeRect(const f64 x, const f64 y, const f64 width, const f64 height, Rect& out){
    if(
        !IsFinite(x) || x < -Limit<f32>::s_Max || x > Limit<f32>::s_Max
        || !IsFinite(y) || y < -Limit<f32>::s_Max || y > Limit<f32>::s_Max
        || !IsFinite(width) || width < 0.0 || width > Limit<f32>::s_Max
        || !IsFinite(height) || height < 0.0 || height > Limit<f32>::s_Max
        || !IsFinite(x + width) || x + width < -Limit<f32>::s_Max || x + width > Limit<f32>::s_Max
        || !IsFinite(y + height) || y + height < -Limit<f32>::s_Max || y + height > Limit<f32>::s_Max
    )
        return false;
    const Rect candidate{ static_cast<f32>(x), static_cast<f32>(y), static_cast<f32>(width), static_cast<f32>(height) };
    if(!IsValidUiRect(candidate))
        return false;
    out = candidate;
    return true;
}

[[nodiscard]] static TextGlyphIntersection::Enum IntersectBounds(
    const f64 left,
    const f64 top,
    const f64 right,
    const f64 bottom,
    const Rect& clip){
    if(
        !IsFinite(left) || left < -Limit<f32>::s_Max || left > Limit<f32>::s_Max
        || !IsFinite(top) || top < -Limit<f32>::s_Max || top > Limit<f32>::s_Max
        || !IsFinite(right) || right < left || right > Limit<f32>::s_Max
        || !IsFinite(bottom) || bottom < top || bottom > Limit<f32>::s_Max
        || right - left > Limit<f32>::s_Max || bottom - top > Limit<f32>::s_Max || !IsValidUiRect(clip)
    )
        return TextGlyphIntersection::Invalid;
    if(
        right <= left || bottom <= top || clip.width <= 0.0f || clip.height <= 0.0f
        || right <= clip.x || bottom <= clip.y
        || left >= static_cast<f64>(clip.x) + clip.width || top >= static_cast<f64>(clip.y) + clip.height
    )
        return TextGlyphIntersection::Invisible;
    return TextGlyphIntersection::Visible;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const BakedFontAtlas* TextGlyphVisibility::selectAtlas(const PlacedGlyph& glyph, const f32 physicalSize){
    if(!glyph.face || !glyph.face->valid() || !IsFinite(physicalSize) || physicalSize <= 0.0f)
        return nullptr;
    const SharedBakedFontAtlas& atlas = glyph.face->bakedAtlas();
    if(
        !atlas || !atlas->valid() || physicalSize < static_cast<f32>(atlas->bakePpem()) * s_BakedFontAtlasMinScale
        || physicalSize > static_cast<f32>(atlas->bakePpem()) * s_BakedFontAtlasMaxScale || !atlas->glyph(glyph.glyphId)
    )
        return nullptr;
    return atlas.get();
}

TextGlyphIntersection::Enum TextGlyphVisibility::candidate(
    const PlacedGlyph& glyph,
    const BakedFontAtlas* const selectedAtlas,
    const f32 fontSize,
    const f32 physicalSize,
    const Point& topLeft,
    const Rect& clip,
    const Point pixelScale){
    using namespace __hidden_ui_glyph_visibility;
    if(
        !ValidLocation(glyph, topLeft) || !ValidFontSize(fontSize) || !IsFinite(physicalSize)
        || physicalSize < 1.0f || physicalSize > 4096.0f || !IsValidUiRect(clip)
        || !IsFinite(pixelScale.x) || !IsFinite(pixelScale.y) || pixelScale.x <= 0.0f || pixelScale.y <= 0.0f
    )
        return TextGlyphIntersection::Invalid;
    if(clip.width <= 0.0f || clip.height <= 0.0f)
        return TextGlyphIntersection::Invisible;
    if(selectedAtlas){
        Rect rectangle;
        if(!atlasRectangle(glyph, *selectedAtlas, fontSize, topLeft, rectangle))
            return TextGlyphIntersection::Invalid;
        return intersect(rectangle, clip);
    }
    if(!IsValidUiRect(glyph.ink) || (glyph.coverage.known && !IsValidUiRect(glyph.coverage.ink)))
        return TextGlyphIntersection::Invalid;
    const Rect& ink = glyph.coverage.known ? glyph.coverage.ink : glyph.ink;
    if(glyph.coverage.known && (ink.width <= 0.0f || ink.height <= 0.0f))
        return TextGlyphIntersection::Invisible;
    if(!glyph.coverage.known){
        if(!glyph.face || !glyph.face->valid() || glyph.face->unitsPerEm() == 0u)
            return TextGlyphIntersection::Invalid;
        if(ink.width <= 0.0f || ink.height <= 0.0f || !glyph.face->coverageInkReliable())
            return TextGlyphIntersection::Visible;
    }
    const f64 shapedSize = Floor(static_cast<f64>(fontSize) * 64.0 + 0.5) / 64.0;
    const f64 correction = glyph.coverage.known ? 1.0 : static_cast<f64>(fontSize) / shapedSize;
    const f64 rasterScale = static_cast<f64>(physicalSize) / fontSize;
    // Native outline bounds include their design-unit allowance; keep raster rounding and physical-pixel snapping conservative.
    const f64 designMargin = glyph.coverage.known ? 0.0 : static_cast<f64>(fontSize) / glyph.face->unitsPerEm();
    const f64 rasterMargin = Max(1.0, correction) / 64.0 + 2.0 / rasterScale + designMargin;
    const f64 marginX = rasterMargin + 0.5 / pixelScale.x;
    const f64 marginY = rasterMargin + 0.5 / pixelScale.y;
    const f64 inkRight = static_cast<f64>(ink.x) + ink.width;
    const f64 inkBottom = static_cast<f64>(ink.y) + ink.height;
    const f64 x = static_cast<f64>(topLeft.x) + glyph.position.x;
    const f64 y = static_cast<f64>(topLeft.y) + glyph.position.y;
    const f64 left = x + Min(static_cast<f64>(ink.x), ink.x * correction) - marginX;
    const f64 top = y + Min(static_cast<f64>(ink.y), ink.y * correction) - marginY;
    const f64 right = x + Max(inkRight, inkRight * correction) + marginX;
    const f64 bottom = y + Max(inkBottom, inkBottom * correction) + marginY;
    return IntersectBounds(left, top, right, bottom, clip);
}

bool TextGlyphVisibility::atlasRectangle(
    const PlacedGlyph& glyph,
    const BakedFontAtlas& atlas,
    const f32 fontSize,
    const Point& topLeft,
    Rect& out){
    using namespace __hidden_ui_glyph_visibility;
    if(!ValidLocation(glyph, topLeft) || !ValidFontSize(fontSize) || !atlas.valid() || atlas.unitsPerEm() == 0u)
        return false;
    const FontAtlasGlyph* record = atlas.glyph(glyph.glyphId);
    if(!record)
        return false;
    if(record->drawable == 0u){
        out = {};
        return true;
    }
    if(
        !IsFinite(record->planeLeft) || !IsFinite(record->planeTop)
        || !IsFinite(record->planeRight) || !IsFinite(record->planeBottom)
        || record->planeRight <= record->planeLeft || record->planeBottom <= record->planeTop
    )
        return false;
    const f64 scale = static_cast<f64>(fontSize) / atlas.unitsPerEm();
    const f64 x = static_cast<f64>(topLeft.x) + glyph.position.x + record->planeLeft * scale;
    const f64 y = static_cast<f64>(topLeft.y) + glyph.position.y + record->planeTop * scale;
    const f64 width = (static_cast<f64>(record->planeRight) - record->planeLeft) * scale;
    const f64 height = (static_cast<f64>(record->planeBottom) - record->planeTop) * scale;
    return MakeRect(x, y, width, height, out);
}

bool TextGlyphVisibility::coverageRectangle(
    const PlacedGlyph& glyph,
    const AtlasGlyph& record,
    const f32 rasterScale,
    const Point& topLeft,
    Rect& out,
    const Point pixelScale){
    using namespace __hidden_ui_glyph_visibility;
    if(
        !ValidLocation(glyph, topLeft) || !IsFinite(rasterScale) || rasterScale <= 0.0f || !IsValidUiRect(record.pixels)
        || !IsFinite(pixelScale.x) || !IsFinite(pixelScale.y) || pixelScale.x <= 0.0f || pixelScale.y <= 0.0f
    )
        return false;
    if(record.pageIndex == s_GlyphAtlasNoPage){
        out = {};
        return true;
    }
    const f64 x = static_cast<f64>(topLeft.x) + glyph.position.x + static_cast<f64>(record.bearingX) / rasterScale;
    const f64 y = static_cast<f64>(topLeft.y) + glyph.position.y - static_cast<f64>(record.bearingY) / rasterScale;
    // FreeType has already rasterized this bitmap on an integer grid; align its texel edges to physical pixels.
    const f64 alignedX = Floor(x * pixelScale.x + 0.5) / pixelScale.x;
    const f64 alignedY = Floor(y * pixelScale.y + 0.5) / pixelScale.y;
    return MakeRect(
        alignedX,
        alignedY,
        record.pixels.width / static_cast<f64>(rasterScale),
        record.pixels.height / static_cast<f64>(rasterScale),
        out
    );
}

TextGlyphIntersection::Enum TextGlyphVisibility::intersect(const Rect& rectangle, const Rect& clip){
    using namespace __hidden_ui_glyph_visibility;
    if(!IsValidUiRect(rectangle) || !IsValidUiRect(clip))
        return TextGlyphIntersection::Invalid;
    return IntersectBounds(
        rectangle.x,
        rectangle.y,
        static_cast<f64>(rectangle.x) + rectangle.width,
        static_cast<f64>(rectangle.y) + rectangle.height,
        clip
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

