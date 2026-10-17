// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"
#include "arena_names.h"

#include <core/alloc/general.h>
#include <core/alloc/scratch.h>

#include <global/filesystem/operations.h>
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


[[nodiscard]] bool Resolve(const NameHash& hash, char* outText, usize outTextSize);

void InstallRuntimeRegistry();
// Detaches callbacks before registry teardown so static use cannot dereference it.
void UninstallRuntimeRegistry()noexcept;
void ClearRuntimeSymbols();

[[nodiscard]] bool WriteDefaultFile();

[[nodiscard]] usize EntryCount();
// Serializes the registry using the current `.namesym` document format.
[[nodiscard]] Expected<void> Serialize(AString<Alloc::GlobalArena>& outText);

// Admits only the current document header before importing symbol records.
[[nodiscard]] bool LoadFromMemory(AStringView text, Alloc::ScratchArena& scratchArena);
[[nodiscard]] bool LoadFromMemory(AStringView text);

struct DefaultFileLoadFailure final{
    ErrorCode code;
    bool loadedAny = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<bool, DefaultFileLoadFailure> LoadDefaultFile();

template<typename ArenaT>
[[nodiscard]] inline Expected<bool, ErrorCode> LoadFile(const ::Path<ArenaT>& path, Alloc::ScratchArena& scratchArena){
    AString<Alloc::ScratchArena> text(scratchArena);
    const auto read = ::ReadTextFile(path, text);
    if(!read)
        return MakeUnexpected(read.error());

    return LoadFromMemory(AStringView(text.data(), text.size()), scratchArena);
}

template<typename ArenaT>
[[nodiscard]] inline Expected<bool, ErrorCode> LoadFile(const ::Path<ArenaT>& path){
    Alloc::ScratchArena scratchArena(CommonArenaScope::s_NameSymbolsScratchArena);
    return LoadFile(path, scratchArena);
}

template<typename CharT, typename ArenaT>
inline void AppendResolvedText(ArenaT& arena, BasicString<CharT, ArenaT>& outText, const AStringView resolvedText){
    if constexpr(IsSame_V<CharT, char>){
        outText.append(resolvedText.data(), resolvedText.size());
    }
    else{
        const BasicString<CharT, ArenaT> converted = StringConvert(arena, resolvedText);
        outText.append(converted.data(), converted.size());
    }
}

template<typename CharT, typename ArenaT>
inline void DecodeHashTokens(ArenaT& arena, BasicString<CharT, ArenaT>& inOutText){
    if(inOutText.size() < ::NameDetail::s_DebugHashTextLength)
        return;

    Optional<BasicString<CharT, ArenaT>> decoded;
    usize copiedUntil = 0u;
    const usize lastTokenOffset = inOutText.size() - ::NameDetail::s_DebugHashTextLength;
    for(usize i = 0u; i <= lastTokenOffset;){
        const auto hashText = ::NameDetail::CopyDebugHashToken<CharT>(BasicStringView<CharT>(inOutText.data(), inOutText.size()), i);
        const auto hash = hashText ? ::NameDetail::DecodeDebugHashText(AStringView(hashText->data(), ::NameDetail::s_DebugHashTextLength)) : Expected<NameHash>(MakeUnexpected(Failure{}));
        if(hash){
            char resolvedText[s_MaxResolvedTextLength];
            if(Resolve(*hash, resolvedText, sizeof(resolvedText))){
                if(!decoded){
                    decoded.emplace(arena);
                    decoded->reserve(inOutText.size());
                }
                decoded->append(inOutText.data() + copiedUntil, i - copiedUntil);
                AppendResolvedText(arena, *decoded, AStringView(resolvedText));
                i += ::NameDetail::s_DebugHashTextLength;
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

