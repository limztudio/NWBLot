// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "font.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using ShapedGlyph = ShapedGlyphRecord<SharedFontFace>;

struct ShapedRun{
    PaintVector<ShapedGlyph> glyphs;
    FontMetrics metrics;


public:
    explicit ShapedRun(Core::Alloc::GlobalArena& arena)noexcept
        : glyphs(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


interface ITextShaper : private NoCopy{
public:
    virtual ~ITextShaper()noexcept = default;


public:
    [[nodiscard]] virtual Expected<ShapedRun, TextLayoutStatus::Enum> shape(const ShapeRequest& request) = 0;
};

class TextShaper final : public ITextShaper{
public:
    explicit TextShaper(Core::Alloc::GlobalArena& arena)noexcept
        : m_arena(arena)
        , m_fonts(arena)
        , m_primary(arena)
        , m_fallback(arena)
        , m_spans(arena)
    {}
    virtual ~TextShaper()noexcept override = default;


public:
    [[nodiscard]] b8 setFonts(Core::Alloc::ScratchArena& scratchArena, Span<const FontSource> sources);
    [[nodiscard]] virtual Expected<ShapedRun, TextLayoutStatus::Enum> shape(const ShapeRequest& request)override;


private:
    Core::Alloc::GlobalArena& m_arena;
    PaintVector<SharedFontFace> m_fonts;
    PaintVector<RawShapedGlyph> m_primary;
    PaintVector<RawShapedGlyph> m_fallback;
    PaintVector<TextFontSpan> m_spans;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

