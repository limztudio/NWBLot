// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "bake.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_SYSTEM_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class FontSource final : NoCopy{
    friend Expected<RasterGlyphs> Rasterize(FontSource& font, const BakeOptions& options);
    friend bool ExportPositioning(const FontSource& font, Impl::FontAtlasPayload& payload);


private:
    [[nodiscard]] static void* Allocate(FT_Memory memory, FT_Long size)noexcept;
    static void Release(FT_Memory memory, void* block)noexcept;
    [[nodiscard]] static void* Reallocate(FT_Memory memory, FT_Long oldSize, FT_Long newSize, void* block)noexcept;


public:
    explicit FontSource(Core::Assets::AssetArena& arena)noexcept;
    ~FontSource()noexcept;


public:
    [[nodiscard]] bool open(const BakeOptions& options, Impl::FontAtlasPayload& payload, Core::Alloc::ScratchArena& scratch);


private:
    Core::Assets::AssetBytes m_bytes;
    FT_MemoryRec_ m_memory{};
    FT_Library m_library = nullptr;
    FT_Face m_face = nullptr;
};

[[nodiscard]] Expected<RasterGlyphs> Rasterize(FontSource& font, const BakeOptions& options);
[[nodiscard]] bool ExportPositioning(const FontSource& font, Impl::FontAtlasPayload& payload);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

