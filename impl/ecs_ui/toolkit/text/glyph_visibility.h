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

// Stateless queries borrow immutable layout and atlas metadata.
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
    [[nodiscard]] static Expected<Rect> AtlasRectangle(
        const PlacedGlyph& glyph,
        const BakedFontAtlas& atlas,
        f32 fontSize,
        const Point& topLeft
    )noexcept;
    [[nodiscard]] static Expected<Rect> CoverageRectangle(
        const PlacedGlyph& glyph,
        const AtlasGlyph& record,
        f32 rasterScale,
        const Point& topLeft,
        Point pixelScale = { 1.0f, 1.0f }
    )noexcept;
    [[nodiscard]] static TextGlyphIntersection::Enum Intersect(const Rect& rectangle, const Rect& clip);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

