// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "basic_string.h"
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


[[nodiscard]] inline bool ParseLinuxProcessMemoryMapLine(const AStringView line, LinuxProcessMemoryMapEntry& outEntry){
    outEntry = LinuxProcessMemoryMapEntry{};

    const usize split = line.find('-');
    if(split == AStringView::npos)
        return false;

    usize rangeEnd = split + 1u;
    while(rangeEnd < line.size() && line[rangeEnd] != ' ' && line[rangeEnd] != '\t')
        ++rangeEnd;

    u64 begin = 0u;
    u64 end = 0u;
    if(
        !ParseVariableHexU64(AStringView(line.data(), split), begin)
        || !ParseVariableHexU64(AStringView(line.data() + split + 1u, rangeEnd - split - 1u), end)
        || begin >= end
    )
        return false;

    usize cursor = 0u;
    if(!ProcessMemoryMapDetail::SkipProcMapField(line, cursor))
        return false;

    ProcessMemoryMapDetail::SkipProcMapWhitespace(line, cursor);
    if(!ProcessMemoryMapDetail::SkipProcMapField(line, cursor))
        return false;

    ProcessMemoryMapDetail::SkipProcMapWhitespace(line, cursor);
    const usize offsetBegin = cursor;
    if(!ProcessMemoryMapDetail::SkipProcMapField(line, cursor))
        return false;

    u64 fileOffset = 0u;
    // The file-offset field is optional in /proc maps lines; a parse failure leaves the pre-initialized default of 0.
    [[maybe_unused]] const bool parsedFileOffset = ParseVariableHexU64(AStringView(line.data() + offsetBegin, cursor - offsetBegin), fileOffset);

    outEntry.begin = begin;
    outEntry.end = end;
    outEntry.fileOffset = fileOffset;
    outEntry.path = ProcessMemoryMapDetail::ProcMapPathField(line);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

