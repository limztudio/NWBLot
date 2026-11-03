// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "atlas.h"
#include "layout.h"

#include <global/simd/result.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Stateless queries borrow immutable layout and atlas metadata.
namespace TextGlyphVisibility{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline const BakedFontAtlas* SelectAtlas(const PlacedGlyph& glyph, const f32 physicalSize)noexcept{
    if(!glyph.face || !glyph.face->valid())
        return nullptr;
    const SharedBakedFontAtlas& atlas = glyph.face->bakedAtlas();
    if(!atlas || !atlas->valid() || !CanUseBakedFontAtlasAtPhysicalSize(atlas->bakePpem(), physicalSize) || !atlas->glyph(glyph.glyphId))
        return nullptr;
    return atlas.get();
}

[[nodiscard]] inline Expected<SIMDVectorDoubleResult> LoadAtlasRectangle(const PlacedGlyph& glyph, const BakedFontAtlas& atlas, const f32 fontSize, const Point& topLeft)noexcept{
    if(!atlas.valid())
        return MakeUnexpected(Failure{});
    const FontAtlasGlyph* record = atlas.glyph(glyph.glyphId);
    if(!record)
        return MakeUnexpected(Failure{});
    const auto candidate = TryLoadGlyphAtlasRectangle(glyph.position, topLeft, fontSize, atlas.unitsPerEm(), *record);
    if(!candidate)
        return MakeUnexpected(Failure{});
    return SIMDVectorDoubleResult{ LoadDouble(*candidate) };
}

[[nodiscard]] inline Expected<Rect> AtlasRectangle(const PlacedGlyph& glyph, const BakedFontAtlas& atlas, const f32 fontSize, const Point& topLeft)noexcept{
    const auto rectangle = LoadAtlasRectangle(glyph, atlas, fontSize, topLeft);
    if(!rectangle)
        return MakeUnexpected(rectangle.error());
    return Rect{
        static_cast<f32>(VectorDoubleGetX(rectangle->value)),
        static_cast<f32>(VectorDoubleGetY(rectangle->value)),
        static_cast<f32>(VectorDoubleGetZ(rectangle->value)),
        static_cast<f32>(VectorDoubleGetW(rectangle->value))
    };
}

// Coverage needs explicit bounds or a valid native face; untrusted native bounds retain conservative preparation.
[[nodiscard]] inline TextGlyphIntersection::Enum Candidate(const PlacedGlyph& glyph, const BakedFontAtlas* const selectedAtlas, const f32 fontSize, const f32 physicalSize, const Point& topLeft, const Rect& clip, const Point pixelScale = { 1.0f, 1.0f })noexcept{
    if(
        !IsValidTextGlyphLocation(glyph.position, topLeft) || !IsValidTextFontSize(fontSize) || !IsFinite(physicalSize)
        || physicalSize < 1.0f || physicalSize > s_TextMaxRasterPpemF32 || !IsValidUiRect(clip)
        || !IsFinite(pixelScale.x) || !IsFinite(pixelScale.y) || pixelScale.x <= 0.0f || pixelScale.y <= 0.0f
    )
        return TextGlyphIntersection::Invalid;
    if(clip.width <= 0.0f || clip.height <= 0.0f)
        return TextGlyphIntersection::Invisible;
    if(selectedAtlas){
        const auto rectangle = LoadAtlasRectangle(glyph, *selectedAtlas, fontSize, topLeft);
        if(!rectangle)
            return TextGlyphIntersection::Invalid;
        const f64 minX = VectorDoubleGetX(rectangle->value);
        const f64 minY = VectorDoubleGetY(rectangle->value);
        const f64 maxX = minX + VectorDoubleGetZ(rectangle->value);
        const f64 maxY = minY + VectorDoubleGetW(rectangle->value);
        return TryIntersectTextBounds(minX, minY, maxX, maxY, clip);
    }
    if(!IsValidUiRect(glyph.ink) || (glyph.coverage.known && !IsValidUiRect(glyph.coverage.ink)))
        return TextGlyphIntersection::Invalid;
    const Rect& ink = glyph.coverage.known ? glyph.coverage.ink : glyph.ink;
    if(glyph.coverage.known && (ink.width <= 0.0f || ink.height <= 0.0f))
        return TextGlyphIntersection::Invisible;
    u32 unitsPerEm = 1u;
    if(!glyph.coverage.known){
        if(!glyph.face || !glyph.face->valid())
            return TextGlyphIntersection::Invalid;
        if(ink.width <= 0.0f || ink.height <= 0.0f || !glyph.face->coverageInkReliable())
            return TextGlyphIntersection::Visible;
        unitsPerEm = glyph.face->unitsPerEm();
    }
    return IntersectTextCoverageInk(glyph.position, topLeft, ink, glyph.coverage.known, unitsPerEm, fontSize, physicalSize, clip, pixelScale);
}

[[nodiscard]] inline Expected<Rect> CoverageRectangle(const PlacedGlyph& glyph, const AtlasGlyph& record, const f32 rasterScale, const Point& topLeft, const Point pixelScale = { 1.0f, 1.0f })noexcept{
    return TryResolveGlyphCoverageRectangle(glyph.position, record.pageIndex, record.pixels, record.bearingX, record.bearingY, rasterScale, topLeft, pixelScale);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
