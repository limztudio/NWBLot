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
    ~TextService();


public:
    [[nodiscard]] bool setFonts(const FontSource* sources, usize count);
    [[nodiscard]] u64 identity()const noexcept{ return m_identity; }
    [[nodiscard]] u64 generation()const noexcept{ return m_generation; }
    [[nodiscard]] Expected<TextLayout, TextLayoutStatus::Enum> layout(const ShapeRequest& request);
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

