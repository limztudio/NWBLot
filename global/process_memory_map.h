// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "basic_string.h"
#include "expected.h"
#include "hash_utils.h"
#include "text_utils.h"
#include "type.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct LinuxProcessMemoryMapEntry{
    u64 begin = 0u;
    u64 end = 0u;
    u64 fileOffset = 0u;
    AStringView path;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ProcessMemoryMapDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_LinuxProcMapPathFieldSkipCount = 5u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void SkipProcMapWhitespace(const AStringView line, usize& cursor)noexcept{
    while(cursor < line.size() && (line[cursor] == ' ' || line[cursor] == '\t'))
        ++cursor;
}

[[nodiscard]] inline bool SkipProcMapField(const AStringView line, usize& cursor)noexcept{
    const usize begin = cursor;
    while(cursor < line.size() && line[cursor] != ' ' && line[cursor] != '\t')
        ++cursor;
    return cursor > begin;
}

[[nodiscard]] inline AStringView ProcMapPathField(AStringView line)noexcept{
    line = TrimLeftView(line);
    for(u32 fieldIndex = 0u; fieldIndex < s_LinuxProcMapPathFieldSkipCount; ++fieldIndex){
        while(!line.empty() && line.front() != ' ' && line.front() != '\t')
            line.remove_prefix(1u);
        line = TrimLeftView(line);
        if(line.empty())
            return AStringView();
    }

    return line;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Expected<LinuxProcessMemoryMapEntry> ParseLinuxProcessMemoryMapLine(const AStringView line)noexcept{
    const usize split = line.find('-');
    if(split == AStringView::npos)
        return MakeUnexpected(Failure{});

    usize rangeEnd = split + 1u;
    while(rangeEnd < line.size() && line[rangeEnd] != ' ' && line[rangeEnd] != '\t')
        ++rangeEnd;

    const auto begin = ParseVariableHexU64(AStringView(line.data(), split));
    const auto end = ParseVariableHexU64(AStringView(line.data() + split + 1u, rangeEnd - split - 1u));
    if(
        !begin
        || !end
        || *begin >= *end
    )
        return MakeUnexpected(Failure{});

    usize cursor = 0u;
    if(!ProcessMemoryMapDetail::SkipProcMapField(line, cursor))
        return MakeUnexpected(Failure{});

    ProcessMemoryMapDetail::SkipProcMapWhitespace(line, cursor);
    if(!ProcessMemoryMapDetail::SkipProcMapField(line, cursor))
        return MakeUnexpected(Failure{});

    ProcessMemoryMapDetail::SkipProcMapWhitespace(line, cursor);
    const usize offsetBegin = cursor;
    if(!ProcessMemoryMapDetail::SkipProcMapField(line, cursor))
        return MakeUnexpected(Failure{});

    // File-offset is optional in /proc maps; failures keep the default of 0.
    const auto fileOffset = ParseVariableHexU64(AStringView(line.data() + offsetBegin, cursor - offsetBegin));
    return LinuxProcessMemoryMapEntry{
        .begin = *begin,
        .end = *end,
        .fileOffset = fileOffset.value_or(0u),
        .path = ProcessMemoryMapDetail::ProcMapPathField(line),
    };
}

template<typename EntryVectorT>
inline void ParseLinuxProcessMemoryMaps(const AStringView mapsText, EntryVectorT& outEntries){
    outEntries.clear();

    usize cursor = 0u;
    while(const auto line = NextTextLine(mapsText, cursor)){
        const auto entry = ParseLinuxProcessMemoryMapLine(*line);
        if(entry)
            outEntries.push_back(*entry);
    }
}

template<typename EntryRangeT>
[[nodiscard]] inline Expected<LinuxProcessMemoryMapEntry> FindLinuxProcessMemoryMapForAddress(
    const EntryRangeT& entries,
    const u64 address
){
    for(const LinuxProcessMemoryMapEntry& entry : entries){
        if(address < entry.begin || address >= entry.end)
            continue;

        return entry;
    }

    return MakeUnexpected(Failure{});
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

