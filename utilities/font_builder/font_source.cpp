// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "font_source.h"
#include "source_input.h"

#include <impl/assets_font/font_validation.h>

#include <core/common/log.h>

#include <global/diagnostics.h>
#include <global/sha256.h>

#include FT_MODULE_H
#include FT_DRIVER_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void* FontSource::Allocate(FT_Memory memory, FT_Long size)noexcept{
    if(size <= 0)
        return nullptr;
    return static_cast<Core::Assets::AssetArena*>(memory->user)->allocate(alignof(MaxAlign), static_cast<usize>(size));
}

void FontSource::Release(FT_Memory memory, void* block)noexcept{
    if(block)
        static_cast<Core::Assets::AssetArena*>(memory->user)->deallocate(block, alignof(MaxAlign), 0u);
}

void* FontSource::Reallocate(FT_Memory memory, FT_Long oldSize, FT_Long newSize, void* block)noexcept{
    static_cast<void>(oldSize);
    if(newSize <= 0){
        Release(memory, block);
        return nullptr;
    }
    return static_cast<Core::Assets::AssetArena*>(memory->user)->reallocate(block, alignof(MaxAlign), static_cast<usize>(newSize));
}

FontSource::FontSource(Core::Assets::AssetArena& arena)noexcept
    : m_bytes(arena)
{
    m_memory.user = &arena;
    m_memory.alloc = &Allocate;
    m_memory.free = &Release;
    m_memory.realloc = &Reallocate;
}

FontSource::~FontSource()noexcept{
    if(m_face){
        const FT_Error result = FT_Done_Face(m_face);
        if(result != 0){
            const DiagnosticEventText diagnostic = MakeDiagnosticEventText("font_builder: failed to destroy FreeType face, error {}", result);
            CaptureDiagnosticEvent("font_builder_lifecycle", diagnostic.view(), __FILE__, __LINE__);
        }
    }
    if(m_library){
        const FT_Error result = FT_Done_Library(m_library);
        if(result != 0){
            const DiagnosticEventText diagnostic = MakeDiagnosticEventText("font_builder: failed to destroy FreeType library, error {}", result);
            CaptureDiagnosticEvent("font_builder_lifecycle", diagnostic.view(), __FILE__, __LINE__);
        }
    }
}

bool FontSource::open(const BakeOptions& options, Impl::FontAtlasPayload& payload, Core::Alloc::ScratchArena& scratch){
    auto bytes = ReadFontSourceInput(options.source, m_bytes.get_allocator().arena(), scratch);
    if(!bytes){
        NWB_LOGGER_ERROR(NWB_TEXT("font_builder: source admission failed: {}"), StringConvert(bytes.error()));
        return false;
    }
    if(!Impl::ValidateFontSource(*bytes, 0u)){
        NWB_LOGGER_ERROR(NWB_TEXT("font_builder: source admission failed"));
        return false;
    }
    m_bytes = Move(*bytes);
    const FT_Error initialized = FT_New_Library(&m_memory, &m_library);
    if(initialized != 0){
        NWB_LOGGER_ERROR(NWB_TEXT("font_builder: FreeType initialization failed, error {}"), initialized);
        return false;
    }
    FT_Add_Default_Modules(m_library);
    const FT_Error admitted = FT_New_Memory_Face(m_library, m_bytes.data(), static_cast<FT_Long>(m_bytes.size()), 0, &m_face);
    if(admitted != 0){
        NWB_LOGGER_ERROR(NWB_TEXT("font_builder: native face admission failed, FreeType error {}"), admitted);
        return false;
    }
    if(m_face->num_glyphs <= 0 || static_cast<u64>(m_face->num_glyphs) > Impl::s_FontAtlasMaxGlyphCount || m_face->units_per_EM == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("font_builder: unsupported glyph count or units per em"));
        return false;
    }
    unsigned int spread = options.spread;
    const AStringView module = options.outline ? "sdf" : "bsdf";
    const FT_Error spreadSet = FT_Property_Set(m_library, module.data(), "spread", &spread);
    if(spreadSet != 0){
        NWB_LOGGER_ERROR(NWB_TEXT("font_builder: FreeType rejected SDF spread, error {}"), spreadSet);
        return false;
    }
    const FT_Error sizeSet = FT_Set_Pixel_Sizes(m_face, 0u, options.ppem);
    if(sizeSet != 0){
        NWB_LOGGER_ERROR(NWB_TEXT("font_builder: FreeType rejected bake size, error {}"), sizeSet);
        return false;
    }
    const ScratchPath output(scratch, options.output);
    const auto stem = PathToGenericString(scratch, output.stem());
    const auto fontReference = Core::Assets::AssetRef<Impl::Font>::FromText(AStringView(stem));
    if(!fontReference){
        NWB_LOGGER_ERROR(NWB_TEXT("font_builder: output asset stem contains an embedded null"));
        return false;
    }
    payload.font = *fontReference;
    payload.fontSha256 = ComputeSha256(BinaryByteView{ .bytes = m_bytes.data(), .byteCount = m_bytes.size() });
    payload.faceIndex = 0u;
    payload.unitsPerEm = m_face->units_per_EM;
    payload.sourceGlyphCount = static_cast<u32>(m_face->num_glyphs);
    payload.bakePpem = options.ppem;
    payload.spreadPixels = options.spread;
    payload.guardTexels = 1u;
    payload.rasterMode = options.outline ? Impl::FontAtlasRasterMode::Outline : Impl::FontAtlasRasterMode::Bitmap;
    payload.ascenderUnits = static_cast<f32>(m_face->ascender);
    payload.descenderUnits = static_cast<f32>(m_face->descender);
    payload.lineGapUnits = static_cast<f32>(m_face->height - m_face->ascender + m_face->descender);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

