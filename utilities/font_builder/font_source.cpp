// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "font_source.h"

#include <impl/assets_font/asset.h>

#include <core/common/log.h>

#include <global/diagnostics.h>
#include <global/sha256.h>

#include FT_MODULE_H
#include FT_DRIVER_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UtilityDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Core::Alloc::GlobalArena& Arena(){
    static Core::Alloc::GlobalArena s_Arena(s_FontBuilderArena);
    return s_Arena;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


FontSource::FontSource(Core::Assets::AssetArena& arena)noexcept
    : m_bytes(arena)
    , m_freeType(arena)
{}

b8 FontSource::open(const BakeOptions& options, Core::Alloc::ScratchArena& scratch, Impl::FontAtlasPayload& payload){
    auto bytes = ReadFontSourceInput(options.source, m_bytes.get_allocator().arena());
    if(!bytes){
        NWB_LOGGER_ERROR(NWB_TEXT("font_builder: source admission failed: {}"), ConvertString(bytes.error()));
        return false;
    }
    m_bytes = Move(*bytes);
    if(!m_freeType.openAndValidate(m_bytes, 0u)){
        NWB_LOGGER_ERROR(NWB_TEXT("font_builder: source admission failed"));
        return false;
    }
    const FT_Library library = m_freeType.library();
    const FT_Face face = m_freeType.face();
    if(face->num_glyphs <= 0 || static_cast<u64>(face->num_glyphs) > Impl::s_FontAtlasMaxGlyphCount || face->units_per_EM == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("font_builder: unsupported glyph count or units per em"));
        return false;
    }
    u32 spread = options.spread;
    const AStringView module = options.outline ? "sdf" : "bsdf";
    const FT_Error spreadSet = FT_Property_Set(library, module.data(), "spread", &spread);
    if(spreadSet != 0){
        NWB_LOGGER_ERROR(NWB_TEXT("font_builder: FreeType rejected SDF spread, error {}"), spreadSet);
        return false;
    }
    const FT_Error sizeSet = FT_Set_Pixel_Sizes(face, 0u, options.ppem);
    if(sizeSet != 0){
        NWB_LOGGER_ERROR(NWB_TEXT("font_builder: FreeType rejected bake size, error {}"), sizeSet);
        return false;
    }
    const auto stem = PathToGenericString(scratch, options.output.stemView());
    const auto fontReference = Core::Assets::AssetRef<Impl::Font>::FromText(AStringView(stem));
    if(!fontReference){
        NWB_LOGGER_ERROR(NWB_TEXT("font_builder: output asset stem contains an embedded null"));
        return false;
    }
    payload.font = *fontReference;
    payload.fontSha256 = ComputeSha256(BinaryByteView{ .bytes = m_bytes.data(), .byteCount = m_bytes.size() });
    payload.faceIndex = 0u;
    payload.unitsPerEm = face->units_per_EM;
    payload.sourceGlyphCount = static_cast<u32>(face->num_glyphs);
    payload.bakePpem = options.ppem;
    payload.spreadPixels = options.spread;
    payload.guardTexels = 1u;
    payload.rasterMode = options.outline ? Impl::FontAtlasRasterMode::Outline : Impl::FontAtlasRasterMode::Bitmap;
    payload.ascenderUnits = static_cast<f32>(face->ascender);
    payload.descenderUnits = static_cast<f32>(face->descender);
    payload.lineGapUnits = static_cast<f32>(face->height - face->ascender + face->descender);
    return true;
}

Core::Assets::AssetBytes FontSource::takeBytes()noexcept{
    m_freeType.reset();
    return Move(m_bytes);
}

b8 ExportPositioning(const FontSource& font, Impl::FontAtlasPayload& payload){
    Core::Assets::AssetArena& arena = payload.positioningTables.get_allocator().arena();
    auto tables = Impl::CopyFontAtlasPositioningTables(font.m_bytes, payload.faceIndex, payload.sourceGlyphCount, false, arena);
    if(!tables)
        return false;
    payload.positioningTables = Move(*tables);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

