// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "asset.h"

#include <core/common/log.h>

#include <global/diagnostics.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MODULE_H
#include FT_SYSTEM_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class FreeTypeMemoryFace final : NoCopy{
private:
    [[nodiscard]] static void* Allocate(FT_Memory memory, const FT_Long size)noexcept{
        if(size <= 0)
            return nullptr;
        auto& arena = *MakeNotNull(static_cast<Core::Assets::AssetArena*>(memory->user));
        return arena.allocate(alignof(MaxAlign), static_cast<usize>(size));
    }

    static void Release(FT_Memory memory, void* block)noexcept{
        if(block){
            auto& arena = *MakeNotNull(static_cast<Core::Assets::AssetArena*>(memory->user));
            arena.deallocate(block, alignof(MaxAlign), 0u);
        }
    }

    [[nodiscard]] static void* Reallocate(FT_Memory memory, [[maybe_unused]] const FT_Long oldSize, const FT_Long newSize, void* block)noexcept{
        if(newSize <= 0){
            Release(memory, block);
            return nullptr;
        }
        auto& arena = *MakeNotNull(static_cast<Core::Assets::AssetArena*>(memory->user));
        return arena.reallocate(block, alignof(MaxAlign), static_cast<usize>(newSize));
    }


public:
    explicit FreeTypeMemoryFace(Core::Assets::AssetArena& arena)noexcept{
        m_memory.user = &arena;
        m_memory.alloc = &Allocate;
        m_memory.free = &Release;
        m_memory.realloc = &Reallocate;
    }

    ~FreeTypeMemoryFace()noexcept{
        reset();
    }


public:
    void reset()noexcept{
        const FT_Face face = m_face;
        const FT_Library library = m_library;
        m_face = nullptr;
        m_library = nullptr;
        const FT_Error faceError = face ? FT_Done_Face(face) : 0;
        const FT_Error libraryError = library ? FT_Done_Library(library) : 0;
        if(faceError != 0){
            const DiagnosticEventText diagnostic = MakeDiagnosticEventText(s_NativeCleanupFailedFormat, faceError);
            CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = s_WarningEventName, .category = s_NativeValidationCategory, .expression = s_FontOpFtDoneFace, .message = diagnostic.view(), .file = __FILE__, .line = __LINE__, });
        }
        if(libraryError != 0){
            const DiagnosticEventText diagnostic = MakeDiagnosticEventText(s_NativeCleanupFailedFormat, libraryError);
            CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = s_WarningEventName, .category = s_NativeValidationCategory, .expression = s_FontOpFtDoneLibrary, .message = diagnostic.view(), .file = __FILE__, .line = __LINE__, });
        }
    }

    [[nodiscard]] Expected<void, NativeFontFailure> openStaticScalableFace(const Span<const u8> bytes)noexcept{
        reset();
        const FT_Error libraryError = FT_New_Library(&m_memory, &m_library);
        if(libraryError != 0)
            return MakeUnexpected(MakeNativeFontFailure(s_FontOpFtNewLibrary, libraryError));
        FT_Add_Default_Modules(m_library);
        const FT_Error faceError = FT_New_Memory_Face(m_library, bytes.data(), static_cast<FT_Long>(bytes.size()), 0, &m_face);
        if(faceError != 0)
            return MakeUnexpected(MakeNativeFontFailure(s_FontOpFtNewMemoryFace, faceError));
        if(m_face->num_faces != 1 || m_face->num_glyphs <= 0 || m_face->units_per_EM == 0u || !FT_IS_SFNT(m_face) || !FT_IS_SCALABLE(m_face) || FT_HAS_MULTIPLE_MASTERS(m_face))
            return MakeUnexpected(MakeNativeFontPolicyFailure(s_FontOpAdmitStaticScalableFace));
        const FT_Error charmapError = FT_Select_Charmap(m_face, FT_ENCODING_UNICODE);
        if(charmapError != 0)
            return MakeUnexpected(MakeNativeFontFailure(s_FontOpFtSelectCharmap, charmapError));
        return {};
    }

    [[nodiscard]] b8 openAndValidate(const Span<const u8> bytes, const u32 faceIndex){
        if(!ValidateFontSourceDirectory(bytes, faceIndex))
            return false;
        const auto nativeValidation = openStaticScalableFace(bytes);
        if(!nativeValidation){
            ReportNativeFontFailure(nativeValidation.error());
            return false;
        }
        return true;
    }

    [[nodiscard]] FT_Library library()const noexcept{ return m_library; }
    [[nodiscard]] FT_Face face()const noexcept{ return m_face; }


private:
    FT_MemoryRec_ m_memory = {};
    FT_Library m_library = nullptr;
    FT_Face m_face = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
