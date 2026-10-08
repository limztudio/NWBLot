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

struct TextCluster{
    u32 byteBegin = 0u;
    u32 byteEnd = 0u;
    u32 firstGlyph = 0u;
    u32 glyphCount = 0u;
    u32 lineIndex = 0u;
    f32 leadingX = 0.0f;
    f32 trailingX = 0.0f;
};

struct TextLine{
    u32 byteBegin = 0u;
    u32 byteEnd = 0u;
    u32 breakEnd = 0u;
    u32 firstGlyph = 0u;
    u32 glyphCount = 0u;
    u32 firstCluster = 0u;
    u32 clusterCount = 0u;
    f32 top = 0.0f;
    f32 baseline = 0.0f;
    f32 height = 0.0f;
    f32 advance = 0.0f;
};

struct TextHit{
    u32 byteOffset = 0u;
    u32 lineIndex = 0u;
    TextCaretEdge::Enum edge = TextCaretEdge::Leading;
    bool inside = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Owns source bytes, layout data, and the exact immutable CPU font versions needed for later glyph rasterization.
class TextLayout final{
    friend class TextLayoutBuilder;


public:
    explicit TextLayout(Core::Alloc::GlobalArena& arena)noexcept;
    TextLayout(TextLayout&&)noexcept = default;
    TextLayout& operator=(TextLayout&&)noexcept = default;


public:
    TextLayout(const TextLayout&) = delete;
    TextLayout& operator=(const TextLayout&) = delete;


public:
    [[nodiscard]] StringView utf8()const noexcept{ return { m_text.data(), m_text.size() }; }
    [[nodiscard]] f32 fontSize()const noexcept{ return m_fontSize; }
    [[nodiscard]] Point measure()const noexcept{ return m_measure; }
    [[nodiscard]] Rect inkBounds()const noexcept{ return m_inkBounds; }
    [[nodiscard]] const PaintVector<PlacedGlyph>& glyphs()const noexcept{ return m_glyphs; }
    [[nodiscard]] const PaintVector<TextCluster>& clusters()const noexcept{ return m_clusters; }
    [[nodiscard]] const PaintVector<TextLine>& lines()const noexcept{ return m_lines; }
    [[nodiscard]] TextHit hitTest(Point point)const noexcept;
    [[nodiscard]] Expected<Rect> caretRect(u32 byteOffset, TextCaretEdge::Enum edge)const noexcept;


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
    TextLayoutBuilder(Core::Alloc::GlobalArena& arena, ITextShaper& shaper)noexcept;


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

