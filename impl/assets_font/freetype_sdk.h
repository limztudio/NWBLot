// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/alloc/general.h>

#include <global/diagnostics.h>
#include <global/inplace_function.h>
#include <global/overflow.h>
#include <global/scope_exit.h>
#include <global/simplemath.h>
#include <global/span.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MODULE_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NWB::FontSdk{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct FreeTypeBitmapView{
    Span<const u8> pixels;
    u32 width;
    u32 rows;
    i32 pitch;
    i32 left;
    i32 top;
    b8 grayscale;
};

struct FreeTypeFaceVisitResult{
    i32 openError = 0;
    i32 closeError = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class FreeTypeFaceView final : NoCopy{
public:
    explicit FreeTypeFaceView(FT_Face face)noexcept : m_face(face){}


public:
    FreeTypeFaceView(FreeTypeFaceView&&) = delete;

    [[nodiscard]] FT_Face handle()const noexcept{ return m_face; }

    [[nodiscard]] i64 faceCount()const noexcept{ return m_face->num_faces; }
    [[nodiscard]] i64 glyphCount()const noexcept{ return m_face->num_glyphs; }
    [[nodiscard]] u32 unitsPerEm()const noexcept{ return m_face->units_per_EM; }
    [[nodiscard]] b8 isSfnt()const noexcept{ return FT_IS_SFNT(m_face) != 0; }
    [[nodiscard]] b8 isScalable()const noexcept{ return FT_IS_SCALABLE(m_face) != 0; }
    [[nodiscard]] b8 hasMultipleMasters()const noexcept{ return FT_HAS_MULTIPLE_MASTERS(m_face) != 0; }
    [[nodiscard]] i32 selectUnicodeCharmap()noexcept{ return FT_Select_Charmap(m_face, FT_ENCODING_UNICODE); }

    [[nodiscard]] i32 setPixelSizes(const u32 width, const u32 height)noexcept{ return FT_Set_Pixel_Sizes(m_face, width, height); }
    [[nodiscard]] i32 loadGlyph(const u32 glyph, const i32 flags)noexcept{ return FT_Load_Glyph(m_face, glyph, flags); }
    [[nodiscard]] i32 renderNormal()noexcept{ return FT_Render_Glyph(m_face->glyph, FT_RENDER_MODE_NORMAL); }
    [[nodiscard]] b8 hasOutline()const noexcept{ return m_face->glyph->format == FT_GLYPH_FORMAT_OUTLINE; }
    [[nodiscard]] i32 outlinePointCount()const noexcept{ return m_face->glyph->outline.n_points; }
    // The pixels remain valid until the face loads or renders its next glyph.
    [[nodiscard]] FreeTypeBitmapView bitmap()const noexcept{
        const FT_GlyphSlot slot = m_face->glyph;
        const FT_Bitmap& bitmap = slot->bitmap;
        const usize pitch = static_cast<usize>(Abs(static_cast<i64>(bitmap.pitch)));
        Span<const u8> pixels;
        if(bitmap.buffer && !MultiplyOverflows<usize>(pitch, bitmap.rows))
            pixels = Span<const u8>(bitmap.buffer, pitch * bitmap.rows);
        return { pixels, bitmap.width, bitmap.rows, bitmap.pitch, slot->bitmap_left, slot->bitmap_top, bitmap.pixel_mode == FT_PIXEL_MODE_GRAY };
    }


private:
    FT_Face m_face;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class FreeTypeLibrary final : NoCopy{
private:
    [[nodiscard]] static void* Allocate(FT_Memory memory, const FT_Long size)noexcept{
        return size > 0 ? static_cast<::ArenaBase*>(memory->user)->allocate(alignof(MaxAlign), static_cast<usize>(size)) : nullptr;
    }
    static void Release(FT_Memory memory, void* block)noexcept{
        if(block)
            static_cast<::ArenaBase*>(memory->user)->deallocate(block, alignof(MaxAlign), 0u);
    }
    [[nodiscard]] static void* Reallocate(FT_Memory memory, [[maybe_unused]] const FT_Long oldSize, const FT_Long newSize, void* block)noexcept{
        if(newSize <= 0){
            Release(memory, block);
            return nullptr;
        }
        return static_cast<::ArenaBase*>(memory->user)->reallocate(block, alignof(MaxAlign), static_cast<usize>(newSize));
    }


public:
    explicit FreeTypeLibrary(::ArenaBase& arena)
        : m_libraryArena(arena.allocationLog())
    {
        m_memory.user = &m_libraryArena;
        m_memory.alloc = &Allocate;
        m_memory.free = &Release;
        m_memory.realloc = &Reallocate;
    }
    ~FreeTypeLibrary()noexcept{
        if(closeLibrary() != 0)
            CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = DiagnosticEventName::s_Error, .category = "impl/assets_font/freetype", .message = "FT_Done_Library failed", .file = __FILE__, .line = __LINE__ });
    }

    FreeTypeLibrary(FreeTypeLibrary&&) = delete;
    [[nodiscard]] FT_Library handle()const noexcept{ return m_library; }


public:
    [[nodiscard]] i32 createLibrary()noexcept{
        if(m_library)
            return FT_Err_Invalid_Argument;
        return FT_New_Library(&m_memory, &m_library);
    }
    void addDefaultModules()noexcept{ FT_Add_Default_Modules(m_library); }
    [[nodiscard]] FreeTypeFaceVisitResult visitMemoryFace(const Span<const u8> bytes, const i64 index, const InplaceFunction<64u, void(FreeTypeFaceView&)>& visitor){
        FT_Face face = nullptr;
        FreeTypeFaceVisitResult result;
        result.openError = openBytes(bytes, index, face);
        if(result.openError != 0)
            return result;
        FreeTypeFaceView view(face);
        ++m_visitors;
        {
            ScopeExit close([&]()noexcept{
                result.closeError = FT_Done_Face(face);
                --m_visitors;
            });
            visitor(view);
        }
        return result;
    }

    [[nodiscard]] i32 closeLibrary()noexcept{
        if(!m_library)
            return 0;
        if(m_visitors != 0u)
            return FT_Err_Invalid_Argument;
        const FT_Error error = FT_Done_Library(m_library);
        m_library = nullptr;
        return error;
    }


public:
    [[nodiscard]] i32 openBytes(const Span<const u8> bytes, const i64 index, FT_Face& face)noexcept{
        if(!m_library || bytes.size() > static_cast<usize>(Limit<FT_Long>::s_Max) || index < Limit<FT_Long>::s_Min || index > Limit<FT_Long>::s_Max)
            return FT_Err_Invalid_Argument;
        return FT_New_Memory_Face(m_library, bytes.data(), static_cast<FT_Long>(bytes.size()), static_cast<FT_Long>(index), &face);
    }


private:
    // FreeType resizes and frees live blocks in arbitrary order.
    Core::Alloc::GlobalArena m_libraryArena;
    FT_MemoryRec_ m_memory{};
    FT_Library m_library = nullptr;
    usize m_visitors = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

