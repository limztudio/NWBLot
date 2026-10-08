// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "font.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ShapedGlyph{
    SharedFontFace face;
    u32 glyphId = 0u;
    u32 byteBegin = 0u;
    u32 byteEnd = 0u;
    Point offset;
    Point advance;
    Rect ink;
    GlyphCoverageBounds coverage{};
};

struct ShapedRun{
    PaintVector<ShapedGlyph> glyphs;
    FontMetrics metrics;

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
private:
    struct FontSpan{
        usize fontIndex = 0u;
        usize glyphBegin = 0u;
        usize glyphEnd = 0u;
        u32 byteBegin = 0u;
        u32 byteEnd = 0u;
    };


public:
    explicit TextShaper(Core::Alloc::GlobalArena& arena);
    virtual ~TextShaper()noexcept override = default;


public:
    [[nodiscard]] bool setFonts(const FontSource* sources, usize count);
    [[nodiscard]] virtual Expected<ShapedRun, TextLayoutStatus::Enum> shape(const ShapeRequest& request)override;


private:
    Core::Alloc::GlobalArena& m_arena;
    PaintVector<SharedFontFace> m_fonts;
    PaintVector<RawShapedGlyph> m_primary;
    PaintVector<RawShapedGlyph> m_fallback;
    PaintVector<FontSpan> m_spans;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

