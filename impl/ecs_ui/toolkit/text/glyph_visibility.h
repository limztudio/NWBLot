// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "layout.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AtlasGlyph;

namespace TextGlyphIntersection{
    enum Enum : u8{ Invalid, Invisible, Visible };
};

// Stateless queries borrow immutable layout/atlas metadata; rejected exact rectangles preserve the caller's output.
class TextGlyphVisibility final{
public:
    [[nodiscard]] static const BakedFontAtlas* SelectAtlas(const PlacedGlyph& glyph, f32 physicalSize);
    // Coverage needs explicit bounds or a valid native face; untrusted native bounds retain conservative preparation.
    [[nodiscard]] static TextGlyphIntersection::Enum Candidate(
        const PlacedGlyph& glyph,
        const BakedFontAtlas* selectedAtlas,
        f32 fontSize,
        f32 physicalSize,
        const Point& topLeft,
        const Rect& clip,
        Point pixelScale = { 1.0f, 1.0f }
    );
    [[nodiscard]] static bool AtlasRectangle(
        const PlacedGlyph& glyph,
        const BakedFontAtlas& atlas,
        f32 fontSize,
        const Point& topLeft,
        Rect& out
    );
    [[nodiscard]] static bool CoverageRectangle(
        const PlacedGlyph& glyph,
        const AtlasGlyph& record,
        f32 rasterScale,
        const Point& topLeft,
        Rect& out,
        Point pixelScale = { 1.0f, 1.0f }
    );
    [[nodiscard]] static TextGlyphIntersection::Enum Intersect(const Rect& rectangle, const Rect& clip);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

