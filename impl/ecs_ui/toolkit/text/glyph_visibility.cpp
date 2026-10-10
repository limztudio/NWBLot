// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "glyph_visibility.h"

#include "atlas.h"

#include <impl/ecs_ui/toolkit/layout/rectangle.h>

#include <global/math/vector_double.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_glyph_visibility{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ValidLocation(const PlacedGlyph& glyph, const Point& topLeft)noexcept{
    return IsFinite(glyph.position.x) && IsFinite(glyph.position.y) && IsFinite(topLeft.x) && IsFinite(topLeft.y);
}

[[nodiscard]] static bool ValidFontSize(const f32 fontSize)noexcept{
    return IsFinite(fontSize) && fontSize >= 1.0f / 64.0f && fontSize <= 2048.0f;
}

[[nodiscard]] static TextGlyphIntersection::Enum IntersectBounds(
    const f64 left,
    const f64 top,
    const f64 right,
    const f64 bottom,
    const Rect& clip
){
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


const BakedFontAtlas* TextGlyphVisibility::SelectAtlas(const PlacedGlyph& glyph, const f32 physicalSize){
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

TextGlyphIntersection::Enum TextGlyphVisibility::Candidate(
    const PlacedGlyph& glyph,
    const BakedFontAtlas* const selectedAtlas,
    const f32 fontSize,
    const f32 physicalSize,
    const Point& topLeft,
    const Rect& clip,
    const Point pixelScale
){
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
        const auto rectangle = AtlasRectangle(glyph, *selectedAtlas, fontSize, topLeft);
        if(!rectangle)
            return TextGlyphIntersection::Invalid;
        return Intersect(*rectangle, clip);
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
    const SIMDVectorDouble marginXMarginYValue = (SIMDVectorDouble{ rasterMargin, rasterMargin } + (SIMDVectorDouble{ 0.5, 0.5 } / SIMDVectorDouble{ pixelScale.x, pixelScale.y }));
    const f64 marginX = marginXMarginYValue.x;
    const f64 marginY = marginXMarginYValue.y;
    const SIMDVectorDouble inkRightInkBottomValue = (SIMDVectorDouble{ static_cast<f64>(ink.x), static_cast<f64>(ink.y) } + SIMDVectorDouble{ ink.width, ink.height });
    const f64 inkRight = inkRightInkBottomValue.x;
    const f64 inkBottom = inkRightInkBottomValue.y;
    const SIMDVectorDouble xYValue = (SIMDVectorDouble{ static_cast<f64>(topLeft.x), static_cast<f64>(topLeft.y) } + SIMDVectorDouble{ glyph.position.x, glyph.position.y });
    const f64 x = xYValue.x;
    const f64 y = xYValue.y;
    const SIMDVectorDouble geometryPair3Operand0 = SIMDVectorDouble{ static_cast<f64>(ink.x), static_cast<f64>(ink.y) };
    const SIMDVectorDouble geometryPair3Operand1 = (SIMDVectorDouble{ ink.x, ink.y } * SIMDVectorDouble{ correction, correction });
    const SIMDVectorDouble leftTopValue = ((SIMDVectorDouble{ x, y } + ((geometryPair3Operand0 < geometryPair3Operand1) ? geometryPair3Operand0 : geometryPair3Operand1)) - SIMDVectorDouble{ marginX, marginY });
    const f64 left = leftTopValue.x;
    const f64 top = leftTopValue.y;
    const SIMDVectorDouble geometryPair4Operand0 = SIMDVectorDouble{ inkRight, inkBottom };
    const SIMDVectorDouble geometryPair4Operand1 = (SIMDVectorDouble{ inkRight, inkBottom } * SIMDVectorDouble{ correction, correction });
    const SIMDVectorDouble rightBottomValue = ((SIMDVectorDouble{ x, y } + ((geometryPair4Operand0 > geometryPair4Operand1) ? geometryPair4Operand0 : geometryPair4Operand1)) + SIMDVectorDouble{ marginX, marginY });
    const f64 right = rightBottomValue.x;
    const f64 bottom = rightBottomValue.y;
    return IntersectBounds(left, top, right, bottom, clip);
}

Expected<Rect> TextGlyphVisibility::AtlasRectangle(
    const PlacedGlyph& glyph,
    const BakedFontAtlas& atlas,
    const f32 fontSize,
    const Point& topLeft
)noexcept{
    using namespace __hidden_ui_glyph_visibility;
    if(!ValidLocation(glyph, topLeft) || !ValidFontSize(fontSize) || !atlas.valid() || atlas.unitsPerEm() == 0u)
        return MakeUnexpected(Failure{});
    const FontAtlasGlyph* record = atlas.glyph(glyph.glyphId);
    if(!record)
        return MakeUnexpected(Failure{});
    if(record->drawable == 0u){
        return Rect{};
    }
    if(
        !IsFinite(record->planeLeft) || !IsFinite(record->planeTop)
        || !IsFinite(record->planeRight) || !IsFinite(record->planeBottom)
        || record->planeRight <= record->planeLeft || record->planeBottom <= record->planeTop
    )
        return MakeUnexpected(Failure{});
    const f64 scale = static_cast<f64>(fontSize) / atlas.unitsPerEm();
    const SIMDVectorDouble xYValue = ((SIMDVectorDouble{ static_cast<f64>(topLeft.x), static_cast<f64>(topLeft.y) } + SIMDVectorDouble{ glyph.position.x, glyph.position.y }) + (SIMDVectorDouble{ record->planeLeft, record->planeTop } * SIMDVectorDouble{ scale, scale }));
    const f64 x = xYValue.x;
    const f64 y = xYValue.y;
    const SIMDVectorDouble widthHeightValue = ((SIMDVectorDouble{ static_cast<f64>(record->planeRight), static_cast<f64>(record->planeBottom) } - SIMDVectorDouble{ record->planeLeft, record->planeTop }) * SIMDVectorDouble{ scale, scale });
    const f64 width = widthHeightValue.x;
    const f64 height = widthHeightValue.y;
    return MakeUiRect<UiRectPrecision::BoundedEndpoints>(x, y, width, height);
}

Expected<Rect> TextGlyphVisibility::CoverageRectangle(
    const PlacedGlyph& glyph,
    const AtlasGlyph& record,
    const f32 rasterScale,
    const Point& topLeft,
    const Point pixelScale
)noexcept{
    using namespace __hidden_ui_glyph_visibility;
    if(
        !ValidLocation(glyph, topLeft) || !IsFinite(rasterScale) || rasterScale <= 0.0f || !IsValidUiRect(record.pixels)
        || !IsFinite(pixelScale.x) || !IsFinite(pixelScale.y) || pixelScale.x <= 0.0f || pixelScale.y <= 0.0f
    )
        return MakeUnexpected(Failure{});
    if(record.pageIndex == s_GlyphAtlasNoPage){
        return Rect{};
    }
    const SIMDVectorDouble origin = SIMDVectorDouble{ topLeft.x, topLeft.y }
        + SIMDVectorDouble{ glyph.position.x, glyph.position.y };
    const SIMDVectorDouble bearing = SIMDVectorDouble{ static_cast<f64>(record.bearingX), static_cast<f64>(record.bearingY) }
        / SIMDVectorDouble{ rasterScale, rasterScale };
    const SIMDVectorDouble positive = origin + bearing;
    const SIMDVectorDouble negative = origin - bearing;
    const f64 x = positive.x;
    const f64 y = negative.y;
    // FreeType has already rasterized this bitmap on an integer grid; align its texel edges to physical pixels.
    const SIMDVectorDouble geometryPair5Operand0 = ((SIMDVectorDouble{ x, y } * SIMDVectorDouble{ pixelScale.x, pixelScale.y }) + SIMDVectorDouble{ 0.5, 0.5 });
    const SIMDVectorDouble alignedXAlignedYValue = (VectorDoubleFloor(geometryPair5Operand0) / SIMDVectorDouble{ pixelScale.x, pixelScale.y });
    const f64 alignedX = alignedXAlignedYValue.x;
    const f64 alignedY = alignedXAlignedYValue.y;
    const SIMDVectorDouble size = SIMDVectorDouble{ record.pixels.width, record.pixels.height }
        / SIMDVectorDouble{ static_cast<f64>(rasterScale), static_cast<f64>(rasterScale) };
    return MakeUiRect<UiRectPrecision::BoundedEndpoints>(alignedX, alignedY, size.x, size.y);
}

TextGlyphIntersection::Enum TextGlyphVisibility::Intersect(const Rect& rectangle, const Rect& clip){
    using namespace __hidden_ui_glyph_visibility;
    if(!IsValidUiRect(rectangle) || !IsValidUiRect(clip))
        return TextGlyphIntersection::Invalid;
    const SIMDVectorDouble end = SIMDVectorDouble{ rectangle.x, rectangle.y }
        + SIMDVectorDouble{ rectangle.width, rectangle.height };
    return IntersectBounds(rectangle.x, rectangle.y, end.x, end.y, clip);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

