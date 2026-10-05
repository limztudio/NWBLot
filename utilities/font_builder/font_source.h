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
private:
    [[nodiscard]] static void* allocate(FT_Memory memory, FT_Long size);
    static void release(FT_Memory memory, void* block);
    [[nodiscard]] static void* reallocate(FT_Memory memory, FT_Long oldSize, FT_Long newSize, void* block);


public:
    explicit FontSource(Core::Assets::AssetArena& arena);
    ~FontSource();


public:
    [[nodiscard]] bool open(const BakeOptions& options, Impl::FontAtlasPayload& payload);
    [[nodiscard]] FT_Face face()const{ return m_face; }
    [[nodiscard]] const Core::Assets::AssetBytes& bytes()const{ return m_bytes; }


private:
    Core::Assets::AssetBytes m_bytes;
    FT_MemoryRec_ m_memory{};
    FT_Library m_library = nullptr;
    FT_Face m_face = nullptr;
};

[[nodiscard]] bool Rasterize(FontSource& font, const BakeOptions& options, RasterGlyphs& outGlyphs);
[[nodiscard]] bool ExportPositioning(const FontSource& font, Impl::FontAtlasPayload& payload);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

