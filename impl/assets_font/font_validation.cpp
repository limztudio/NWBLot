// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset.h"

#include "freetype_face.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void* FreeTypeMemoryFace::Allocate(FT_Memory memory, const FT_Long size)noexcept{
    if(size <= 0)
        return nullptr;
    auto& arena = *MakeNotNull(static_cast<Core::Assets::AssetArena*>(memory->user));
    return arena.allocate(alignof(MaxAlign), static_cast<usize>(size));
}

void FreeTypeMemoryFace::Release(FT_Memory memory, void* block)noexcept{
    if(block){
        auto& arena = *MakeNotNull(static_cast<Core::Assets::AssetArena*>(memory->user));
        arena.deallocate(block, alignof(MaxAlign), 0u);
    }
}

void* FreeTypeMemoryFace::Reallocate(FT_Memory memory, [[maybe_unused]] const FT_Long oldSize, const FT_Long newSize, void* block)noexcept{
    if(newSize <= 0){
        Release(memory, block);
        return nullptr;
    }
    auto& arena = *MakeNotNull(static_cast<Core::Assets::AssetArena*>(memory->user));
    return arena.reallocate(block, alignof(MaxAlign), static_cast<usize>(newSize));
}

FreeTypeMemoryFace::FreeTypeMemoryFace(Core::Assets::AssetArena& arena)noexcept{
    m_memory.user = &arena;
    m_memory.alloc = &Allocate;
    m_memory.free = &Release;
    m_memory.realloc = &Reallocate;
}

FreeTypeMemoryFace::~FreeTypeMemoryFace()noexcept{
    reset();
}

void FreeTypeMemoryFace::reset()noexcept{
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

Expected<void, NativeFontFailure> FreeTypeMemoryFace::openStaticScalableFace(const Span<const u8> bytes)noexcept{
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

b8 FreeTypeMemoryFace::openAndValidate(const Span<const u8> bytes, const u32 faceIndex){
    if(faceIndex != 0u || bytes.empty() || bytes.size() > s_FontMaxSourceBytes){
        NWB_LOGGER_ERROR(NWB_TEXT("ValidateFontSource failed: face index must be zero and source size must be 1..{} bytes"), s_FontMaxSourceBytes);
        return false;
    }
    if(!ValidateFontSfntRequiredTables(bytes)){
        NWB_LOGGER_ERROR(NWB_TEXT("ValidateFontSource failed: unsupported or malformed static TrueType/CFF SFNT directory"));
        return false;
    }
    const auto nativeValidation = openStaticScalableFace(bytes);
    if(!nativeValidation){
        const auto& failure = nativeValidation.error();
        if(failure.hasNativeError)
            NWB_LOGGER_ERROR(NWB_TEXT("ValidateFontSource failed: native_operation={} native_domain=FreeType native_code={}"), ConvertString(failure.operation), failure.nativeError);
        else
            NWB_LOGGER_ERROR(NWB_TEXT("ValidateFontSource failed: FreeType rejected the scalable Unicode face ({})"), ConvertString(failure.operation));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ValidateFontSource(const Core::Assets::AssetBytes& bytes, const u32 faceIndex){
    FreeTypeMemoryFace validator(bytes.get_allocator().arena());
    return validator.openAndValidate(Span<const u8>(bytes), faceIndex);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

