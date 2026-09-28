// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "font_validation.h"

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
static constexpr usize s_SfntHeaderBytes = 12u;
static constexpr usize s_TableRecordBytes = 16u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static u16 ReadBigU16(const u8* data){
    return static_cast<u16>((static_cast<u16>(data[0u]) << 8u) | data[1u]);
}

[[nodiscard]] static u32 ReadBigU32(const u8* data){
    return
        (static_cast<u32>(data[0u]) << 24u)
        | (static_cast<u32>(data[1u]) << 16u)
        | (static_cast<u32>(data[2u]) << 8u)
        | static_cast<u32>(data[3u])
    ;
}

[[nodiscard]] static bool ValidateSfntDirectory(const Core::Assets::AssetBytes& bytes){
    if(bytes.size() < s_SfntHeaderBytes)
        return false;
    const u32 signature = ReadBigU32(bytes.data());
    if(signature != s_TrueTypeSignature && signature != s_CffSignature)
        return false;
    const u16 tableCount = ReadBigU16(bytes.data() + 4u);
    const usize directoryBytes = s_SfntHeaderBytes + static_cast<usize>(tableCount) * s_TableRecordBytes;
    if(tableCount == 0u || tableCount > s_FontMaxTableCount || directoryBytes > bytes.size())
        return false;

    bool hasHead = false;
    bool hasCmap = false;
    bool hasMaxp = false;
    bool hasHhea = false;
    bool hasHmtx = false;
    bool hasGlyf = false;
    bool hasLoca = false;
    bool hasCff = false;
    for(u16 index = 0u; index < tableCount; ++index){
        const u8* record = bytes.data() + s_SfntHeaderBytes + static_cast<usize>(index) * s_TableRecordBytes;
        const u32 tag = ReadBigU32(record);
        const u32 offset = ReadBigU32(record + 8u);
        const u32 length = ReadBigU32(record + 12u);
        if(offset < directoryBytes || static_cast<u64>(offset) + length > bytes.size())
            return false;
        if(tag == 0x66766172u || tag == 0x43464632u) // fvar, CFF2
            return false;
        for(u16 previous = 0u; previous < index; ++previous){
            const u8* previousRecord = bytes.data() + s_SfntHeaderBytes + static_cast<usize>(previous) * s_TableRecordBytes;
            if(ReadBigU32(previousRecord) == tag)
                return false;
        }
        switch(tag){
        case 0x68656164u: hasHead = length >= 54u; break;
        case 0x636d6170u: hasCmap = length >= 4u; break;
        case 0x6d617870u: hasMaxp = length >= 6u; break;
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
    [[nodiscard]] static void* allocate(FT_Memory memory, const FT_Long size){
        if(size <= 0)
            return nullptr;
        auto& arena = *static_cast<Core::Assets::AssetArena*>(memory->user);
        return arena.allocate(alignof(MaxAlign), static_cast<usize>(size));
    }

    static void release(FT_Memory memory, void* block){
        if(block){
            auto& arena = *static_cast<Core::Assets::AssetArena*>(memory->user);
            arena.deallocate(block, alignof(MaxAlign), 0u);
        }
    }

    [[nodiscard]] static void* reallocate(FT_Memory memory, const FT_Long oldSize, const FT_Long newSize, void* block){
        static_cast<void>(oldSize);
        if(newSize <= 0){
            release(memory, block);
            return nullptr;
        }
        auto& arena = *static_cast<Core::Assets::AssetArena*>(memory->user);
        return arena.reallocate(block, alignof(MaxAlign), static_cast<usize>(newSize));
    }


public:
    explicit NativeFontValidator(Core::Assets::AssetArena& arena){
        m_memory.user = &arena;
        m_memory.alloc = &allocate;
        m_memory.free = &release;
        m_memory.realloc = &reallocate;
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

