// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <core/alloc/general.h>

#include <global/filesystem/directory_iterator.h>
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
inline constexpr usize s_DebugHashTextLength = NameDetail::s_DebugHashTextLength;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using NameDetail::CopyDebugHashToken;
using NameDetail::DecodeDebugHashText;
using NameDetail::IsNameHashTokenChar;

[[nodiscard]] bool Resolve(const NameHash& hash, char* outText, usize outTextSize);

void InstallRuntimeRegistry();
// Detaches callbacks before registry teardown so static use cannot dereference it.
void UninstallRuntimeRegistry();
void ClearRuntimeSymbols();

[[nodiscard]] bool WriteDefaultFile();

[[nodiscard]] usize EntryCount();
// Serializes the registry using the current `.namesym` document format.
void Serialize(AString<Alloc::GlobalArena>& outText);

// Admits only the current document header before importing symbol records.
[[nodiscard]] bool LoadFromMemory(AStringView text);

template<typename ArenaT>
[[nodiscard]] inline bool LoadFile(const ::Path<ArenaT>& path){
    AString<ArenaT> text(path.arena());
    if(!::ReadTextFile(path, text))
        return false;

    return LoadFromMemory(AStringView(text.data(), text.size()));
}

template<typename ArenaT>
[[nodiscard]] inline bool LoadDefaultFile(ArenaT& arena){
    ::Path<ArenaT> executableDirectory(arena);
    if(!::GetExecutableDirectory(executableDirectory))
        return false;

    bool loadedAny = false;

    ErrorCode error;
    ::DirectoryIterator directory(executableDirectory, error);
    if(error)
        return loadedAny;

    for(const ::DirectoryEntry<ArenaT>& entry : directory){
        ErrorCode fileError;
        if(!entry.isRegularFile(fileError) || fileError)
            continue;

        const ::Path<ArenaT> extensionPath = entry.path().extension();
        if(extensionPath.native() != s_FileExtension)
            continue;

        loadedAny = LoadFile(entry.path()) || loadedAny;
    }

    return loadedAny;
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
    if(inOutText.size() < s_DebugHashTextLength)
        return;

    Optional<BasicString<CharT, ArenaT>> decoded;
    usize copiedUntil = 0u;
    const usize lastTokenOffset = inOutText.size() - s_DebugHashTextLength;
    for(usize i = 0u; i <= lastTokenOffset;){
        char hashText[s_DebugHashTextLength + 1u] = {};
        NameHash hash = {};
        char resolvedText[s_MaxResolvedTextLength] = {};
        if(
            CopyDebugHashToken<CharT>(BasicStringView<CharT>(inOutText.data(), inOutText.size()), i, hashText)
            && DecodeDebugHashText(AStringView(hashText, s_DebugHashTextLength), hash)
            && Resolve(hash, resolvedText, sizeof(resolvedText))
        ){
            if(!decoded){
                decoded.emplace(arena);
                decoded->reserve(inOutText.size());
            }
            decoded->append(inOutText.data() + copiedUntil, i - copiedUntil);
            AppendResolvedText(arena, *decoded, AStringView(resolvedText));
            i += s_DebugHashTextLength;
            copiedUntil = i;
            continue;
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

