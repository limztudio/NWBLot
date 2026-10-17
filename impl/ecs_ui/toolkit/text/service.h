// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "layout.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GlyphAtlas;

// Construct, set fonts, layout, and paint on one owning UI thread. The arena outlives all layouts/pages/snapshots.
class TextService final : NoCopy{
public:
    explicit TextService(Core::Alloc::GlobalArena& arena);
    ~TextService()noexcept;


public:
    [[nodiscard]] bool setFonts(Core::Alloc::ScratchArena& scratchArena, const FontSource* sources, usize count);
    [[nodiscard]] u64 identity()const noexcept{ return m_identity; }
    [[nodiscard]] u64 generation()const noexcept{ return m_generation; }
    [[nodiscard]] Expected<TextLayout, TextLayoutStatus::Enum> layout(const ShapeRequest& request);
    // Logical extent plus visible glyph support for this display, including every native pixel-snap phase; no atlas admission or line/caret mutation.
    // Size from width/height and paint at contentOrigin - { x, y } to keep negative bearings and marks inside the content rectangle.
    [[nodiscard]] Expected<Rect> measure(const TextLayout& layout, const DisplayMetrics& display);
    [[nodiscard]] bool paint(PaintBuilder& paint, const TextLayout& layout, Point topLeft, const Color& color = {});


private:
    TextShaper m_shaper;
    TextLayoutBuilder m_layoutBuilder;
    NotNullUniquePtr<GlyphAtlas, ArenaDeleter<GlyphAtlas, Core::Alloc::GlobalArena>> m_atlas;
    PaintVector<SharedGlyphPage> m_pages;
    PaintVector<SharedSdfAtlasPage> m_sdfPages;
    u64 m_identity = 0u;
    u64 m_generation = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

