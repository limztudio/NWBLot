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


inline constexpr f64 s_RasterMarginPixels = 2.0;
inline constexpr f64 s_FixedPointRoundBias = 0.5;
inline constexpr f64 s_HalfPixel = 0.5;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ValidLocation(const PlacedGlyph& glyph, const Point& topLeft)noexcept{
    return IsFinite(glyph.position.x) && IsFinite(glyph.position.y) && IsFinite(topLeft.x) && IsFinite(topLeft.y);
}

[[nodiscard]] static bool ValidFontSize(const f32 fontSize)noexcept{
    return IsFinite(fontSize) && fontSize >= 1.0f / s_TextFixedPointScale && fontSize <= s_TextMaxFontSize;
}

[[nodiscard]] static TextGlyphIntersection::Enum IntersectBounds(const SIMDVectorDouble minimum, const SIMDVectorDouble maximum, const SIMDVectorSingle clip)noexcept{
    const SIMDVectorDouble limit = VectorDoubleReplicate(static_cast<f64>(Limit<f32>::s_Max));
    if(
        !VectorDoubleIsFinite(minimum, VectorDoubleComponentMask::s_DoubleXY) || !VectorDoubleIsFinite(maximum, VectorDoubleComponentMask::s_DoubleXY)
        || (VectorDoubleMoveMask(VectorDoubleAndInt(VectorDoubleInBounds(minimum, limit), VectorDoubleAndInt(VectorDoubleInBounds(maximum, limit), VectorDoubleGreaterOrEqual(maximum, minimum)))) & VectorDoubleComponentMask::s_DoubleXY) != VectorDoubleComponentMask::s_DoubleXY
    )
        return TextGlyphIntersection::Invalid;
    // Both sides are finite and bounded here, so this f64 subtract cannot overflow.
    const SIMDVectorDouble extent = VectorDoubleSubtract(maximum, minimum);
    if((VectorDoubleMoveMask(VectorDoubleGreater(extent, limit)) & VectorDoubleComponentMask::s_DoubleXY) != 0u)
        return TextGlyphIntersection::Invalid;
    const SIMDVectorDouble clipDouble = ConvertVectorSingleToDouble(clip);
    if(
        (VectorDoubleMoveMask(VectorDoubleLessOrEqual(maximum, minimum)) & VectorDoubleComponentMask::s_DoubleXY) != 0u
        || (VectorMoveMask(VectorLessOrEqual(VectorSwizzle<2, 3, 2, 3>(clip), VectorZero())) & VectorComponentMask::s_XY) != 0u
        || (VectorDoubleMoveMask(VectorDoubleLessOrEqual(maximum, clipDouble)) & VectorDoubleComponentMask::s_DoubleXY) != 0u
    )
        return TextGlyphIntersection::Invisible;
    // Callers pass a valid clip, so these f64 sums are finite.
    const SIMDVectorDouble clipEnd = VectorDoubleAdd(clipDouble, VectorDoubleSwizzle<2, 3, 2, 3>(clipDouble));
    if((VectorDoubleMoveMask(VectorDoubleGreaterOrEqual(minimum, clipEnd)) & VectorDoubleComponentMask::s_DoubleXY) != 0u)
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
        || physicalSize < 1.0f || physicalSize > s_TextMaxRasterPpemF32 || !IsValidUiRect(clip)
        || !IsFinite(pixelScale.x) || !IsFinite(pixelScale.y) || pixelScale.x <= 0.0f || pixelScale.y <= 0.0f
    )
        return TextGlyphIntersection::Invalid;
    if(clip.width <= 0.0f || clip.height <= 0.0f)
        return TextGlyphIntersection::Invisible;
    const SIMDVectorSingle clipVector = VectorSet(clip.x, clip.y, clip.width, clip.height);
    if(selectedAtlas){
        const auto rectangle = LoadAtlasRectangle(glyph, *selectedAtlas, fontSize, topLeft);
        if(!rectangle)
            return TextGlyphIntersection::Invalid;
        const SIMDVectorDouble origin = VectorDoubleAndInt(rectangle->value, s_DoubleSIMDMaskXY);
        return IntersectBounds(origin, VectorDoubleAdd(origin, VectorDoubleSwizzle<2, 3, 2, 3>(rectangle->value)), clipVector);
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
    const SIMDVectorDouble shapedSizeValue = VectorDoubleDivide(VectorDoubleFloor(VectorDoubleAdd(VectorDoubleMultiply(VectorDoubleReplicate(fontSize), VectorDoubleReplicate(s_TextFixedPointScaleF64)), VectorDoubleReplicate(s_FixedPointRoundBias))), VectorDoubleReplicate(s_TextFixedPointScaleF64));
    const SIMDVectorDouble scaleNumerators = VectorDoubleSet(glyph.coverage.known ? 1.0 : static_cast<f64>(fontSize), physicalSize, glyph.coverage.known ? 0.0 : static_cast<f64>(fontSize), 0.0);
    const SIMDVectorDouble scaleDenominators = VectorDoubleSelect(VectorDoubleSet(1.0, fontSize, glyph.coverage.known ? 1.0 : static_cast<f64>(glyph.face->unitsPerEm()), 1.0), shapedSizeValue, glyph.coverage.known ? VectorDoubleZero() : s_DoubleSIMDMaskX);
    const SIMDVectorDouble scales = VectorDoubleDivide(scaleNumerators, scaleDenominators);
    const SIMDVectorDouble correction = VectorDoubleSplatX(scales);
    // Native outline bounds include their design-unit allowance; keep raster rounding and physical-pixel snapping conservative.
    const SIMDVectorDouble rasterMarginValue = VectorDoubleAdd(
        VectorDoubleAdd(VectorDoubleDivide(VectorDoubleMax(VectorDoubleSplatOne(), correction), VectorDoubleReplicate(s_TextFixedPointScaleF64)), VectorDoubleDivide(VectorDoubleReplicate(s_RasterMarginPixels), VectorDoubleSplatY(scales))),
        VectorDoubleSplatZ(scales)
    );
    const SIMDVectorDouble pixelScaleVector = VectorDoubleSet(pixelScale.x, pixelScale.y, 1.0, 1.0);
    const SIMDVectorDouble margin = VectorDoubleAdd(VectorDoubleAndInt(rasterMarginValue, s_DoubleSIMDMaskXY), VectorDoubleDivide(VectorDoubleSet(s_HalfPixel, s_HalfPixel, 0.0, 0.0), pixelScaleVector));
    const SIMDVectorDouble inkOrigin = VectorDoubleSet(ink.x, ink.y, 0.0, 0.0);
    const SIMDVectorDouble inkEnd = VectorDoubleAdd(inkOrigin, VectorDoubleSet(ink.width, ink.height, 0.0, 0.0));
    const SIMDVectorDouble position = VectorDoubleAdd(VectorDoubleSet(topLeft.x, topLeft.y, 0.0, 0.0), VectorDoubleSet(glyph.position.x, glyph.position.y, 0.0, 0.0));
    const SIMDVectorDouble correctionVector = VectorDoubleAndInt(correction, s_DoubleSIMDMaskXY);
    const SIMDVectorDouble correctedOrigin = VectorDoubleMultiply(inkOrigin, correctionVector);
    const SIMDVectorDouble minimumOrigin = VectorDoubleSelect(correctedOrigin, inkOrigin, VectorDoubleLess(inkOrigin, correctedOrigin));
    const SIMDVectorDouble leftTop = VectorDoubleSubtract(VectorDoubleAdd(position, minimumOrigin), margin);
    const SIMDVectorDouble correctedEnd = VectorDoubleMultiply(inkEnd, correctionVector);
    const SIMDVectorDouble maximumEnd = VectorDoubleSelect(correctedEnd, inkEnd, VectorDoubleGreater(inkEnd, correctedEnd));
    const SIMDVectorDouble rightBottom = VectorDoubleAdd(VectorDoubleAdd(position, maximumEnd), margin);
    return IntersectBounds(leftTop, rightBottom, clipVector);
}

Expected<SIMDVectorDoubleResult> TextGlyphVisibility::LoadAtlasRectangle(const PlacedGlyph& glyph, const BakedFontAtlas& atlas, const f32 fontSize, const Point& topLeft)noexcept{
    using namespace __hidden_ui_glyph_visibility;
    if(!ValidLocation(glyph, topLeft) || !ValidFontSize(fontSize) || !atlas.valid() || atlas.unitsPerEm() == 0u)
        return MakeUnexpected(Failure{});
    const FontAtlasGlyph* record = atlas.glyph(glyph.glyphId);
    if(!record)
        return MakeUnexpected(Failure{});
    if(record->drawable == 0u)
        return SIMDVectorDoubleResult{ VectorDoubleZero() };
    if(!IsFinite(record->planeLeft) || !IsFinite(record->planeTop) || !IsFinite(record->planeRight) || !IsFinite(record->planeBottom) || record->planeRight <= record->planeLeft || record->planeBottom <= record->planeTop)
        return MakeUnexpected(Failure{});
    const SIMDVectorDouble origin = VectorDoubleAdd(VectorDoubleSet(topLeft.x, topLeft.y, 0.0, 0.0), VectorDoubleSet(glyph.position.x, glyph.position.y, 0.0, 0.0));
    const SIMDVectorDouble planeOrigin = VectorDoubleSet(record->planeLeft, record->planeTop, 0.0, 0.0);
    const SIMDVectorDouble scaleVector = VectorDoubleDivide(VectorDoubleSet(static_cast<f64>(fontSize), static_cast<f64>(fontSize), 0.0, 0.0), VectorDoubleSet(static_cast<f64>(atlas.unitsPerEm()), static_cast<f64>(atlas.unitsPerEm()), 1.0, 1.0));
    const SIMDVectorDouble xYValue = VectorDoubleMultiplyAddExpression(planeOrigin, scaleVector, origin);
    const SIMDVectorDouble planeEnd = VectorDoubleSet(record->planeRight, record->planeBottom, 0.0, 0.0);
    const SIMDVectorDouble planeExtent = VectorDoubleSubtract(planeEnd, planeOrigin);
    const SIMDVectorDouble widthHeightValue = VectorDoubleMultiply(planeExtent, scaleVector);
    const SIMDVectorDouble candidate = VectorDoublePermute<0, 1, 4, 5>(xYValue, widthHeightValue);
    const SIMDVectorDouble limit = VectorDoubleReplicate(static_cast<f64>(Limit<f32>::s_Max));
    const SIMDVectorDouble end = VectorDoubleAdd(VectorDoubleAndInt(candidate, s_DoubleSIMDMaskXY), VectorDoubleAndInt(VectorDoubleSwizzle<2, 3, 0, 1>(candidate), s_DoubleSIMDMaskXY));
    if(
        !VectorDoubleIsFinite(candidate, VectorDoubleComponentMask::s_DoubleXYZW)
        || (VectorDoubleMoveMask(VectorDoubleInBounds(candidate, limit)) & VectorDoubleComponentMask::s_DoubleXYZW) != VectorDoubleComponentMask::s_DoubleXYZW
        || (VectorDoubleMoveMask(VectorDoubleLess(VectorDoubleSwizzle<2, 3, 2, 3>(candidate), VectorDoubleZero())) & VectorDoubleComponentMask::s_DoubleXY) != 0u
        || (VectorDoubleMoveMask(VectorDoubleInBounds(end, limit)) & VectorDoubleComponentMask::s_DoubleXY) != VectorDoubleComponentMask::s_DoubleXY
    )
        return MakeUnexpected(Failure{});
    const SIMDVectorSingle rounded = ConvertVectorDoubleToSingle(candidate);
    if(!VectorIsFinite(rounded, VectorComponentMask::s_XYZW) || !VectorIsFinite(VectorAdd(rounded, VectorSwizzle<2, 3, 0, 1>(rounded)), VectorComponentMask::s_XY))
        return MakeUnexpected(Failure{});
    return SIMDVectorDoubleResult{ ConvertVectorSingleToDouble(rounded) };
}

Expected<Rect> TextGlyphVisibility::AtlasRectangle(const PlacedGlyph& glyph, const BakedFontAtlas& atlas, const f32 fontSize, const Point& topLeft)noexcept{
    const auto rectangle = LoadAtlasRectangle(glyph, atlas, fontSize, topLeft);
    if(!rectangle)
        return MakeUnexpected(rectangle.error());
    return Rect{ static_cast<f32>(VectorDoubleGetX(rectangle->value)), static_cast<f32>(VectorDoubleGetY(rectangle->value)), static_cast<f32>(VectorDoubleGetZ(rectangle->value)), static_cast<f32>(VectorDoubleGetW(rectangle->value)) };
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
    const SIMDVectorDouble biased = VectorDoubleMultiplyAddExpression(position, pixelScaleVector, VectorDoubleSet(s_HalfPixel, s_HalfPixel, 0.0, 0.0));
    const SIMDVectorDouble aligned = VectorDoubleDivide(VectorDoubleFloor(biased), VectorDoubleSet(pixelScale.x, pixelScale.y, 1.0, 1.0));
    const SIMDVectorDouble size = VectorDoubleDivide(VectorDoubleSet(record.pixels.width, record.pixels.height, 0.0, 0.0), VectorDoubleReplicate(static_cast<f64>(rasterScale)));
    return MakeUiRect<UiRectPrecision::BoundedEndpoints>(VectorDoublePermute<0, 1, 4, 5>(aligned, size));
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

