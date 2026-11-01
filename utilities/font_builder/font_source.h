// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "bake.h"

#include <impl/assets_font/freetype_face.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class FontSource final : NoCopy{
    friend Expected<RasterGlyphs> Rasterize(const BakeOptions& options, FontSource& inOutFont);
    friend b8 ExportPositioning(const FontSource& font, Impl::FontAtlasPayload& payload);


public:
    explicit FontSource(Core::Assets::AssetArena& arena)noexcept;
    ~FontSource()noexcept = default;


public:
    [[nodiscard]] b8 open(const BakeOptions& options, Core::Alloc::ScratchArena& scratch, Impl::FontAtlasPayload& payload);
    [[nodiscard]] Span<const u8> bytes()const noexcept{ return m_bytes; }
    [[nodiscard]] Core::Assets::AssetBytes takeBytes()noexcept;


private:
    Core::Assets::AssetBytes m_bytes;
    Impl::FreeTypeMemoryFace m_freeType;
};

[[nodiscard]] Expected<RasterGlyphs> Rasterize(const BakeOptions& options, FontSource& inOutFont);
[[nodiscard]] b8 ExportPositioning(const FontSource& font, Impl::FontAtlasPayload& payload);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

