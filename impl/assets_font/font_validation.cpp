// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "font_validation.h"
#include "source_directory.h"

#include <core/common/log.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MODULE_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_font_validation{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr u32 s_TrueTypeSignature = 0x00010000u;
static constexpr u32 s_CffSignature = 0x4f54544fu; // OTTO


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ValidateSfntDirectory(const Core::Assets::AssetBytes& bytes)noexcept{
    const auto directoryResult = FontSfntDirectory::Read({ bytes.data(), bytes.size() });
    if(!directoryResult)
        return false;
    const FontSfntDirectory& directory = *directoryResult;
    const u32 signature = directory.signature();
    if(signature != s_TrueTypeSignature && signature != s_CffSignature)
        return false;

    bool hasHead = false;
    bool hasCmap = false;
    bool hasMaxp = false;
    bool hasHhea = false;
    bool hasHmtx = false;
    bool hasGlyf = false;
    bool hasLoca = false;
    bool hasCff = false;
    for(u16 index = 0u; index < directory.tableCount(); ++index){
        const auto tableResult = directory.table(index);
        if(!tableResult || tableResult->offset < directory.directoryBytes())
            return false;
        const u32 tag = tableResult->tag;
        const usize length = tableResult->bytes.size();
        if(tag == 0x66766172u || tag == 0x43464632u) // fvar, CFF2
            return false;
        for(u16 previous = 0u; previous < index; ++previous){
            const auto previousTagResult = directory.tableTag(previous);
            if(!previousTagResult || *previousTagResult == tag)
                return false;
        }
        switch(tag){
        case s_FontSfntHeadTag: hasHead = length >= 54u; break;
        case 0x636d6170u: hasCmap = length >= 4u; break;
        case s_FontSfntMaxpTag: hasMaxp = length >= 6u; break;
        case 0x68686561u: hasHhea = length >= 36u; break;
        case 0x686d7478u: hasHmtx = length >= 4u; break;
        case 0x676c7966u: hasGlyf = length > 0u; break;
        case 0x6c6f6361u: hasLoca = length > 0u; break;
        case 0x43464620u: hasCff = length > 0u; break;
        default: break;
        }
    }
    return
        hasHead && hasCmap && hasMaxp && hasHhea && hasHmtx
        && (signature == s_TrueTypeSignature ? hasGlyf && hasLoca : hasCff)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class NativeFontValidator final : NoCopy{
private:
    [[nodiscard]] static void* Allocate(FT_Memory memory, const FT_Long size){
        if(size <= 0)
            return nullptr;
        auto& arena = *static_cast<Core::Assets::AssetArena*>(memory->user);
        return arena.allocate(alignof(MaxAlign), static_cast<usize>(size));
    }

    static void Release(FT_Memory memory, void* block){
        if(block){
            auto& arena = *static_cast<Core::Assets::AssetArena*>(memory->user);
            arena.deallocate(block, alignof(MaxAlign), 0u);
        }
    }

    [[nodiscard]] static void* Reallocate(FT_Memory memory, const FT_Long oldSize, const FT_Long newSize, void* block){
        static_cast<void>(oldSize);
        if(newSize <= 0){
            Release(memory, block);
            return nullptr;
        }
        auto& arena = *static_cast<Core::Assets::AssetArena*>(memory->user);
        return arena.reallocate(block, alignof(MaxAlign), static_cast<usize>(newSize));
    }


public:
    explicit NativeFontValidator(Core::Assets::AssetArena& arena){
        m_memory.user = &arena;
        m_memory.alloc = &Allocate;
        m_memory.free = &Release;
        m_memory.realloc = &Reallocate;
    }
    ~NativeFontValidator(){
        if(m_face)
            FT_Done_Face(m_face);
        if(m_library)
            FT_Done_Library(m_library);
    }


public:
    [[nodiscard]] bool validate(const Core::Assets::AssetBytes& bytes){
        const FT_Error libraryError = FT_New_Library(&m_memory, &m_library);
        if(libraryError != 0)
            return false;
        FT_Add_Default_Modules(m_library);
        const FT_Error faceError = FT_New_Memory_Face(m_library, bytes.data(), static_cast<FT_Long>(bytes.size()), 0, &m_face);
        if(faceError != 0)
            return false;
        return
            m_face->num_faces == 1 && m_face->num_glyphs > 0 && m_face->units_per_EM > 0u
            && FT_IS_SFNT(m_face) && FT_IS_SCALABLE(m_face) && !FT_HAS_MULTIPLE_MASTERS(m_face)
            && FT_Select_Charmap(m_face, FT_ENCODING_UNICODE) == 0
        ;
    }


private:
    FT_MemoryRec_ m_memory = {};
    FT_Library m_library = nullptr;
    FT_Face m_face = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ValidateFontSource(const Core::Assets::AssetBytes& bytes, const u32 faceIndex){
    if(faceIndex != 0u || bytes.empty() || bytes.size() > s_FontMaxSourceBytes){
        NWB_LOGGER_ERROR(NWB_TEXT("ValidateFontSource failed: face index must be zero and source size must be 1..{} bytes"), s_FontMaxSourceBytes);
        return false;
    }
    if(!__hidden_font_validation::ValidateSfntDirectory(bytes)){
        NWB_LOGGER_ERROR(NWB_TEXT("ValidateFontSource failed: unsupported or malformed static TrueType/CFF SFNT directory"));
        return false;
    }
    __hidden_font_validation::NativeFontValidator validator(bytes.get_allocator().arena());
    if(!validator.validate(bytes)){
        NWB_LOGGER_ERROR(NWB_TEXT("ValidateFontSource failed: FreeType rejected the scalable Unicode face"));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

