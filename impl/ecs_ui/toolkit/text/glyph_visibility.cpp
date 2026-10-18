// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "glyph_visibility.h"

#include "atlas.h"

#include <impl/ecs_ui/toolkit/layout/rectangle.h>

#include <global/simd/vector.h>
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

[[nodiscard]] static TextGlyphIntersection::Enum IntersectBounds(const SIMDVectorDouble minimum, const SIMDVectorDouble maximum, const SIMDVectorSingle clip)noexcept{
    const f64 left = VectorDoubleGetX(minimum);
    const f64 top = VectorDoubleGetY(minimum);
    const f64 right = VectorDoubleGetX(maximum);
    const f64 bottom = VectorDoubleGetY(maximum);
    const f32 clipX = VectorGetX(clip);
    const f32 clipY = VectorGetY(clip);
    const f32 clipWidth = VectorGetZ(clip);
    const f32 clipHeight = VectorGetW(clip);
    if(
        !IsFinite(left) || left < -Limit<f32>::s_Max || left > Limit<f32>::s_Max
        || !IsFinite(top) || top < -Limit<f32>::s_Max || top > Limit<f32>::s_Max
        || !IsFinite(right) || right < left || right > Limit<f32>::s_Max
        || !IsFinite(bottom) || bottom < top || bottom > Limit<f32>::s_Max
    )
        return TextGlyphIntersection::Invalid;
    // Both sides are finite and bounded here, so this f64 subtract cannot overflow.
    const SIMDVectorDouble extent = VectorDoubleSubtract(maximum, minimum);
    if(VectorDoubleGetX(extent) > Limit<f32>::s_Max || VectorDoubleGetY(extent) > Limit<f32>::s_Max)
        return TextGlyphIntersection::Invalid;
    if(right <= left || bottom <= top || clipWidth <= 0.0f || clipHeight <= 0.0f || right <= clipX || bottom <= clipY)
        return TextGlyphIntersection::Invisible;
    // Callers pass a valid clip, so these f64 sums are finite.
    const SIMDVectorDouble clipEnd = VectorDoubleAdd(VectorDoubleSet(clipX, clipY, 0.0, 0.0), VectorDoubleSet(clipWidth, clipHeight, 0.0, 0.0));
    if(left >= VectorDoubleGetX(clipEnd) || top >= VectorDoubleGetY(clipEnd))
        return TextGlyphIntersection::Invisible;
    return TextGlyphIntersection::Visible;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const BakedFontAtlas* TextGlyphVisibility::SelectAtlas(const PlacedGlyph& glyph, const f32 physicalSize)noexcept{
    if(!glyph.face || !glyph.face->valid() || !IsFinite(physicalSize) || physicalSize <= 0.0f)
        return nullptr;
    const SharedBakedFontAtlas& atlas = glyph.face->bakedAtlas();
    if(!atlas || !atlas->valid())
        return nullptr;
    const SIMDVectorSingle sizeRange = VectorScale(VectorSet(s_BakedFontAtlasMinScale, s_BakedFontAtlasMaxScale, 0.0f, 0.0f), static_cast<f32>(atlas->bakePpem()));
    if(physicalSize < VectorGetX(sizeRange) || physicalSize > VectorGetY(sizeRange) || !atlas->glyph(glyph.glyphId))
        return nullptr;
    return atlas.get();
}

TextGlyphIntersection::Enum TextGlyphVisibility::Candidate(const PlacedGlyph& glyph, const BakedFontAtlas* const selectedAtlas, const f32 fontSize, const f32 physicalSize, const Point& topLeft, const Rect& clip, const Point pixelScale)noexcept{
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
    const SIMDVectorDouble shapedSizeValue = VectorDoubleDivide(VectorDoubleFloor(VectorDoubleAdd(VectorDoubleMultiply(VectorDoubleReplicate(fontSize), VectorDoubleReplicate(64.0)), VectorDoubleReplicate(0.5))), VectorDoubleReplicate(64.0));
    const f64 shapedSize = VectorDoubleGetX(shapedSizeValue);
    const SIMDVectorDouble scaleNumerators = VectorDoubleSet(glyph.coverage.known ? 1.0 : static_cast<f64>(fontSize), physicalSize, glyph.coverage.known ? 0.0 : static_cast<f64>(fontSize), 0.0);
    const SIMDVectorDouble scaleDenominators = VectorDoubleSet(glyph.coverage.known ? 1.0 : shapedSize, fontSize, glyph.coverage.known ? 1.0 : static_cast<f64>(glyph.face->unitsPerEm()), 1.0);
    const SIMDVectorDouble scales = VectorDoubleDivide(scaleNumerators, scaleDenominators);
    const f64 correction = VectorDoubleGetX(scales);
    const f64 rasterScale = VectorDoubleGetY(scales);
    // Native outline bounds include their design-unit allowance; keep raster rounding and physical-pixel snapping conservative.
    const f64 designMargin = VectorDoubleGetZ(scales);
    const SIMDVectorDouble rasterMarginValue = VectorDoubleAdd(
        VectorDoubleAdd(VectorDoubleDivide(VectorDoubleMax(VectorDoubleSplatOne(), VectorDoubleReplicate(correction)), VectorDoubleReplicate(64.0)), VectorDoubleDivide(VectorDoubleReplicate(2.0), VectorDoubleReplicate(rasterScale))),
        VectorDoubleReplicate(designMargin)
    );
    const f64 rasterMargin = VectorDoubleGetX(rasterMarginValue);
    const SIMDVectorDouble pixelScaleVector = VectorDoubleSet(pixelScale.x, pixelScale.y, 1.0, 1.0);
    const SIMDVectorDouble margin = VectorDoubleAdd(VectorDoubleSet(rasterMargin, rasterMargin, 0.0, 0.0), VectorDoubleDivide(VectorDoubleSet(0.5, 0.5, 0.0, 0.0), pixelScaleVector));
    const SIMDVectorDouble inkOrigin = VectorDoubleSet(ink.x, ink.y, 0.0, 0.0);
    const SIMDVectorDouble inkEnd = VectorDoubleAdd(inkOrigin, VectorDoubleSet(ink.width, ink.height, 0.0, 0.0));
    const SIMDVectorDouble position = VectorDoubleAdd(VectorDoubleSet(topLeft.x, topLeft.y, 0.0, 0.0), VectorDoubleSet(glyph.position.x, glyph.position.y, 0.0, 0.0));
    const SIMDVectorDouble correctionVector = VectorDoubleSet(correction, correction, 0.0, 0.0);
    const SIMDVectorDouble correctedOrigin = VectorDoubleMultiply(inkOrigin, correctionVector);
    const SIMDVectorDouble minimumOrigin = VectorDoubleSelect(correctedOrigin, inkOrigin, VectorDoubleLess(inkOrigin, correctedOrigin));
    const SIMDVectorDouble leftTop = VectorDoubleSubtract(VectorDoubleAdd(position, minimumOrigin), margin);
    const SIMDVectorDouble correctedEnd = VectorDoubleMultiply(inkEnd, correctionVector);
    const SIMDVectorDouble maximumEnd = VectorDoubleSelect(correctedEnd, inkEnd, VectorDoubleGreater(inkEnd, correctedEnd));
    const SIMDVectorDouble rightBottom = VectorDoubleAdd(VectorDoubleAdd(position, maximumEnd), margin);
    return IntersectBounds(leftTop, rightBottom, VectorSet(clip.x, clip.y, clip.width, clip.height));
}

Expected<Rect> TextGlyphVisibility::AtlasRectangle(const PlacedGlyph& glyph, const BakedFontAtlas& atlas, const f32 fontSize, const Point& topLeft)noexcept{
    using namespace __hidden_ui_glyph_visibility;
    if(!ValidLocation(glyph, topLeft) || !ValidFontSize(fontSize) || !atlas.valid() || atlas.unitsPerEm() == 0u)
        return MakeUnexpected(Failure{});
    const FontAtlasGlyph* record = atlas.glyph(glyph.glyphId);
    if(!record)
        return MakeUnexpected(Failure{});
    if(record->drawable == 0u){
        return Rect{};
    }
    if(!IsFinite(record->planeLeft) || !IsFinite(record->planeTop) || !IsFinite(record->planeRight) || !IsFinite(record->planeBottom) || record->planeRight <= record->planeLeft || record->planeBottom <= record->planeTop)
        return MakeUnexpected(Failure{});
    const SIMDVectorDouble origin = VectorDoubleAdd(VectorDoubleSet(topLeft.x, topLeft.y, 0.0, 0.0), VectorDoubleSet(glyph.position.x, glyph.position.y, 0.0, 0.0));
    const SIMDVectorDouble planeOrigin = VectorDoubleSet(record->planeLeft, record->planeTop, 0.0, 0.0);
    const SIMDVectorDouble scaleVector = VectorDoubleDivide(VectorDoubleSet(static_cast<f64>(fontSize), static_cast<f64>(fontSize), 0.0, 0.0), VectorDoubleSet(static_cast<f64>(atlas.unitsPerEm()), static_cast<f64>(atlas.unitsPerEm()), 1.0, 1.0));
    const SIMDVectorDouble xYValue = VectorDoubleMultiplyAddExpression(planeOrigin, scaleVector, origin);
    const f64 x = VectorDoubleGetX(xYValue);
    const f64 y = VectorDoubleGetY(xYValue);
    const SIMDVectorDouble planeEnd = VectorDoubleSet(record->planeRight, record->planeBottom, 0.0, 0.0);
    const SIMDVectorDouble widthHeightValue = VectorDoubleMultiply(VectorDoubleSubtract(planeEnd, planeOrigin), scaleVector);
    const f64 width = VectorDoubleGetX(widthHeightValue);
    const f64 height = VectorDoubleGetY(widthHeightValue);
    return MakeUiRect<UiRectPrecision::BoundedEndpoints>(x, y, width, height);
}

Expected<Rect> TextGlyphVisibility::CoverageRectangle(const PlacedGlyph& glyph, const AtlasGlyph& record, const f32 rasterScale, const Point& topLeft, const Point pixelScale)noexcept{
    using namespace __hidden_ui_glyph_visibility;
    if(!ValidLocation(glyph, topLeft) || !IsFinite(rasterScale) || rasterScale <= 0.0f || !IsValidUiRect(record.pixels) || !IsFinite(pixelScale.x) || !IsFinite(pixelScale.y) || pixelScale.x <= 0.0f || pixelScale.y <= 0.0f)
        return MakeUnexpected(Failure{});
    if(record.pageIndex == s_GlyphAtlasNoPage){
        return Rect{};
    }
    const SIMDVectorDouble origin = VectorDoubleAdd(VectorDoubleSet(topLeft.x, topLeft.y, 0.0, 0.0), VectorDoubleSet(glyph.position.x, glyph.position.y, 0.0, 0.0));
    const SIMDVectorDouble bearing = VectorDoubleDivide(VectorDoubleSet(static_cast<f64>(record.bearingX), static_cast<f64>(record.bearingY), 0.0, 0.0), VectorDoubleReplicate(rasterScale));
    const SIMDVectorDouble positive = VectorDoubleAdd(origin, bearing);
    const SIMDVectorDouble negative = VectorDoubleSubtract(origin, bearing);
    const SIMDVectorDouble position = VectorDoubleSelect(positive, negative, s_DoubleSIMDMaskY);
    // FreeType has already rasterized this bitmap on an integer grid; align its texel edges to physical pixels.
    const SIMDVectorDouble pixelScaleVector = VectorDoubleSet(pixelScale.x, pixelScale.y, 0.0, 0.0);
    const SIMDVectorDouble biased = VectorDoubleMultiplyAddExpression(position, pixelScaleVector, VectorDoubleSet(0.5, 0.5, 0.0, 0.0));
    const SIMDVectorDouble aligned = VectorDoubleDivide(VectorDoubleFloor(biased), VectorDoubleSet(pixelScale.x, pixelScale.y, 1.0, 1.0));
    const SIMDVectorDouble size = VectorDoubleDivide(VectorDoubleSet(record.pixels.width, record.pixels.height, 0.0, 0.0), VectorDoubleReplicate(static_cast<f64>(rasterScale)));
    return MakeUiRect<UiRectPrecision::BoundedEndpoints>(VectorDoubleGetX(aligned), VectorDoubleGetY(aligned), VectorDoubleGetX(size), VectorDoubleGetY(size));
}

TextGlyphIntersection::Enum TextGlyphVisibility::Intersect(const Rect& rectangle, const Rect& clip)noexcept{
    using namespace __hidden_ui_glyph_visibility;
    if(!IsValidUiRect(rectangle) || !IsValidUiRect(clip))
        return TextGlyphIntersection::Invalid;
    const SIMDVectorDouble end = VectorDoubleAdd(VectorDoubleSet(rectangle.x, rectangle.y, 0.0, 0.0), VectorDoubleSet(rectangle.width, rectangle.height, 0.0, 0.0));
    return IntersectBounds(VectorDoubleSet(rectangle.x, rectangle.y, 0.0, 0.0), end, VectorSet(clip.x, clip.y, clip.width, clip.height));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

