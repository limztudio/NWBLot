// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "shaper.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct PlacedGlyph{
    SharedFontFace face;
    u32 glyphId = 0u;
    u32 byteBegin = 0u;
    u32 byteEnd = 0u;
    Point position;
    // Shaping ink is relative to the glyph baseline origin; an empty rectangle is conservatively unknown at paint time.
    Rect ink{};
    GlyphCoverageBounds coverage{};
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Owns source bytes, layout data, and the exact immutable CPU font versions needed for later glyph rasterization.
class TextLayout final{
    friend class TextLayoutBuilder;


public:
    explicit TextLayout(Core::Alloc::GlobalArena& arena)noexcept
        : m_text(arena)
        , m_glyphs(arena)
        , m_clusters(arena)
        , m_lines(arena)
    {}
    TextLayout(TextLayout&&)noexcept = default;
    TextLayout(const TextLayout&) = delete;


public:
    TextLayout& operator=(TextLayout&&)noexcept = default;

    TextLayout& operator=(const TextLayout&) = delete;


public:
    [[nodiscard]] StringView utf8()const noexcept{ return { m_text.data(), m_text.size() }; }

    [[nodiscard]] f32 fontSize()const noexcept{ return m_fontSize; }

    [[nodiscard]] Point measure()const noexcept{ return m_measure; }

    [[nodiscard]] Rect inkBounds()const noexcept{ return m_inkBounds; }

    [[nodiscard]] const PaintVector<PlacedGlyph>& glyphs()const noexcept{ return m_glyphs; }

    [[nodiscard]] const PaintVector<TextCluster>& clusters()const noexcept{ return m_clusters; }

    [[nodiscard]] const PaintVector<TextLine>& lines()const noexcept{ return m_lines; }

    [[nodiscard]] TextHit hitTest(const Point point)const noexcept{ return HitTestTextLayout(m_lines, m_clusters, point); }
    [[nodiscard]] Expected<Rect> caretRect(const u32 byteOffset, const TextCaretEdge::Enum edge)const noexcept{
        Rect rect;
        if(!TryResolveTextCaretRect(m_lines, m_clusters, m_direction, byteOffset, edge, rect))
            return MakeUnexpected(Failure{});
        return rect;
    }


private:
    AString<Core::Alloc::GlobalArena> m_text;
    PaintVector<PlacedGlyph> m_glyphs;
    PaintVector<TextCluster> m_clusters;
    PaintVector<TextLine> m_lines;
    Point m_measure;
    Rect m_inkBounds;
    f32 m_fontSize = 0.0f;
    TextDirection::Enum m_direction = TextDirection::LeftToRight;
};

class TextLayoutBuilder final : NoCopy{
public:
    TextLayoutBuilder(Core::Alloc::GlobalArena& arena, ITextShaper& shaper)noexcept
        : m_arena(arena)
        , m_shaper(shaper)
    {}


public:
    // Line breaks are LF/CRLF; there is no automatic wrap or paragraph bidi.
    [[nodiscard]] Expected<TextLayout, TextLayoutStatus::Enum> layout(const ShapeRequest& request);


private:
    Core::Alloc::GlobalArena& m_arena;
    ITextShaper& m_shaper;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

