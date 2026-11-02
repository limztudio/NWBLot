// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"
#include "arena_names.h"

#include <core/alloc/general.h>
#include <core/alloc/scratch.h>

#include <global/filesystem/operations_file_io.h>
#include <global/name.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_COMMON_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NameSymbols{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_MaxResolvedTextLength = 1024u;
inline constexpr TStringView s_FileExtension = NWB_TEXT(".namesym");
inline constexpr AStringView s_FileHeader = "nwb_namesym_v1";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 Resolve(char* outText, usize outTextSize, const NameHash& hash);

void InstallRuntimeRegistry();
// Detaches callbacks before registry teardown so static use cannot dereference it.
void UninstallRuntimeRegistry()noexcept;
void ClearRuntimeSymbols();

[[nodiscard]] b8 WriteDefaultFile();

[[nodiscard]] usize EntryCount();
// Serializes the registry using the current `.namesym` document format.
[[nodiscard]] Expected<void> Serialize(AString<Alloc::GlobalArena>& outText);

// Admits only the current document header before importing symbol records.
[[nodiscard]] b8 LoadFromMemory(AStringView text, Alloc::ScratchArena& scratchArena);

struct DefaultFileLoadFailure final{
    ErrorCode code;
    b8 loadedAny = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<b8, DefaultFileLoadFailure> LoadDefaultFile();

template<typename ArenaT>
[[nodiscard]] inline Expected<b8, ErrorCode> LoadFile(const ::Path<ArenaT>& path, Alloc::ScratchArena& scratchArena){
    AString<Alloc::ScratchArena> text(scratchArena);
    const auto read = ::ReadTextFile(path, text);
    if(!read)
        return MakeUnexpected(read.error());

    return LoadFromMemory(AStringView(text.data(), text.size()), scratchArena);
}

template<typename CharT, typename ArenaT>
inline void AppendResolvedText(const AStringView resolvedText, ArenaT& arena, BasicString<CharT, ArenaT>& outText){
    if constexpr(IsSame_V<CharT, char>){
        outText.append(resolvedText.data(), resolvedText.size());
    }
    else{
        const BasicString<CharT, ArenaT> converted = ConvertString(arena, resolvedText);
        outText.append(converted.data(), converted.size());
    }
}

template<typename CharT, typename ArenaT>
inline void DecodeHashTokens(ArenaT& arena, BasicString<CharT, ArenaT>& inOutText){
    if(inOutText.size() < s_DebugHashTextLength)
        return;

    Optional<BasicString<CharT, ArenaT>> decoded;
    usize copiedUntil = 0u;
    const usize lastTokenOffset = inOutText.size() - s_DebugHashTextLength;
    for(usize i = 0u; i <= lastTokenOffset;){
        const auto hashText = CopyDebugHashToken<CharT>(BasicStringView<CharT>(inOutText.data(), inOutText.size()), i);
        const auto hash = hashText ? DecodeDebugHashText(AStringView(hashText->data(), s_DebugHashTextLength)) : Expected<NameHash>(MakeUnexpected(Failure{}));
        if(hash){
            Array<char, s_MaxResolvedTextLength> resolvedText;
            if(Resolve(resolvedText.data(), resolvedText.size(), *hash)){
                if(!decoded){
                    decoded.emplace(arena);
                    decoded->reserve(inOutText.size());
                }
                decoded->append(inOutText.data() + copiedUntil, i - copiedUntil);
                AppendResolvedText(AStringView(resolvedText.data()), arena, *decoded);
                i += s_DebugHashTextLength;
                copiedUntil = i;
                continue;
            }
        }

        ++i;
    }

    if(decoded){
        decoded->append(inOutText.data() + copiedUntil, inOutText.size() - copiedUntil);
        inOutText = Move(*decoded);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_COMMON_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

