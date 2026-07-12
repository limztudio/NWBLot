
#pragma once


#include "global.h"

#include <global/core/alloc/general.h>

#include <global/filesystem/directory_iterator.h>
#include <global/filesystem/operations.h>
#include <global/name.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_COMMON_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NameSymbols{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_MaxResolvedTextLength = 1024u;
inline constexpr char s_FileName[] = "namesym";
inline constexpr char s_FileExtension[] = ".namesym";
inline constexpr char s_FileHeader[] = "nwb_namesym_v1";
inline constexpr usize s_DebugHashTextLength = (16u * NameDetail::s_HashLaneCount) + (NameDetail::s_HashLaneCount - 1u);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool IsNameHashTokenChar(const char ch){
    return (ch >= '0' && ch <= '9')
        || (ch >= 'a' && ch <= 'f')
        || (ch >= 'A' && ch <= 'F')
        || ch == '_'
    ;
}
template<typename CharT>
[[nodiscard]] inline bool IsNameHashTokenChar(const CharT ch){
    return (ch >= static_cast<CharT>('0') && ch <= static_cast<CharT>('9'))
        || (ch >= static_cast<CharT>('a') && ch <= static_cast<CharT>('f'))
        || (ch >= static_cast<CharT>('A') && ch <= static_cast<CharT>('F'))
        || ch == static_cast<CharT>('_')
    ;
}

[[nodiscard]] bool DecodeDebugHashText(AStringView text, NameHash& outHash);

template<typename CharT>
[[nodiscard]] inline bool CopyDebugHashToken(const BasicStringView<CharT> text, const usize offset, char (&outHashText)[s_DebugHashTextLength + 1u]){
    if(offset + s_DebugHashTextLength > text.size())
        return false;
    if(offset > 0u && IsNameHashTokenChar(text[offset - 1u]))
        return false;
    if(offset + s_DebugHashTextLength < text.size() && IsNameHashTokenChar(text[offset + s_DebugHashTextLength]))
        return false;

    for(usize i = 0u; i < s_DebugHashTextLength; ++i){
        const CharT ch = text[offset + i];
        if(((i + 1u) % 17u) == 0u){
            if(ch != static_cast<CharT>('_'))
                return false;
        }
        else if(!IsNameHashTokenChar(ch) || ch == static_cast<CharT>('_')){
            return false;
        }

        outHashText[i] = static_cast<char>(ch);
    }
    outHashText[s_DebugHashTextLength] = 0;
    return true;
}

[[nodiscard]] bool Resolve(const NameHash& hash, char* outText, usize outTextSize);

void InstallRuntimeRegistry();
// Detaches the record/resolve callbacks from name.h so no Name::c_str()/ctor reaches the RuntimeRegistry afterwards.
// Must run before the RuntimeRegistry's function-local static is destroyed at process exit -- otherwise a Name
// resolved/recorded during static teardown would dereference the destroyed registry (use-after-free).
void UninstallRuntimeRegistry();
void ClearRuntimeSymbols();

[[nodiscard]] bool LoadLine(AStringView line);
[[nodiscard]] bool WriteDefaultFile();

// Number of symbols currently in the runtime registry. A client uses this as a cheap "grew since last upload" gate
// for the cross-process symbol upload.
[[nodiscard]] usize EntryCount();
// Serializes the whole runtime registry to the `.namesym` text format (same bytes WriteDefaultFile would write),
// appended to outText. Used to push the symbol table to a remote log server over the wire.
void Serialize(AString<Alloc::GlobalArena>& outText);

// Ingest a whole `.namesym` document (newline-separated) into the runtime registry. The transport (file or wire)
// only differs in where the bytes come from; both feed each line to LoadLine. Used by LoadFile + the server's
// /namesym upload handler.
[[nodiscard]] inline bool LoadFromMemory(const AStringView text){
    bool loadedAny = false;
    usize lineBegin = 0u;
    while(lineBegin < text.size()){
        usize lineEnd = lineBegin;
        while(lineEnd < text.size() && text[lineEnd] != '\n')
            ++lineEnd;

        AStringView line(text.data() + lineBegin, lineEnd - lineBegin);
        if(!line.empty() && line.back() == '\r')
            line.remove_suffix(1u);
        if(LoadLine(line))
            loadedAny = true;

        lineBegin = lineEnd < text.size() ? lineEnd + 1u : lineEnd;
    }

    return loadedAny;
}

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

    bool loadedAny = LoadFile(executableDirectory / s_FileName);

    ErrorCode error;
    ::DirectoryIterator directory(executableDirectory, error);
    if(error)
        return loadedAny;

    for(const ::DirectoryEntry<ArenaT>& entry : directory){
        ErrorCode fileError;
        if(!entry.is_regular_file(fileError) || fileError)
            continue;

        const AString<ArenaT> extension = PathToString<char>(arena, entry.path().extension());
        if(extension != AStringView(s_FileExtension))
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
[[nodiscard]] inline bool DecodeHashTokens(ArenaT& arena, BasicString<CharT, ArenaT>& inOutText){
    if(inOutText.size() < s_DebugHashTextLength)
        return false;

    BasicString<CharT, ArenaT> decoded(arena);
    decoded.reserve(inOutText.size());

    bool changed = false;
    for(usize i = 0u; i < inOutText.size();){
        char hashText[s_DebugHashTextLength + 1u] = {};
        NameHash hash = {};
        char resolvedText[s_MaxResolvedTextLength] = {};
        if(
            CopyDebugHashToken<CharT>(BasicStringView<CharT>(inOutText.data(), inOutText.size()), i, hashText)
            && DecodeDebugHashText(AStringView(hashText, s_DebugHashTextLength), hash)
            && Resolve(hash, resolvedText, sizeof(resolvedText))
        ){
            AppendResolvedText(arena, decoded, AStringView(resolvedText));
            i += s_DebugHashTextLength;
            changed = true;
            continue;
        }

        decoded += inOutText[i];
        ++i;
    }

    if(changed)
        inOutText = Move(decoded);
    return changed;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_COMMON_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

