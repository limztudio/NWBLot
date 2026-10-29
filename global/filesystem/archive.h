// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../basic_string.h"
#include "../limit.h"
#include "../type.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GlobalFilesystemArchiveDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u8 s_MinSafeArchivePathCharacter = 0x20u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT>
[[nodiscard]] inline b8 IsArchivePathSeparator(const CharT ch)noexcept(IsArithmetic_V<CharT>){
    return ch == static_cast<CharT>('/') || ch == static_cast<CharT>('\\');
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline b8 IsSafeArchiveRelativePath(const AStringView pathText)noexcept{
    if(pathText.empty() || GlobalFilesystemArchiveDetail::IsArchivePathSeparator(pathText.front()))
        return false;

    usize segmentBegin = 0u;
    for(usize i = 0u; i <= pathText.size(); ++i){
        const b8 atEnd = i == pathText.size();
        const char ch = atEnd ? '/' : pathText[i];
        if(!atEnd && (static_cast<unsigned char>(ch) < GlobalFilesystemArchiveDetail::s_MinSafeArchivePathCharacter || ch == ':'))
            return false;

        if(!atEnd && !GlobalFilesystemArchiveDetail::IsArchivePathSeparator(ch))
            continue;

        const usize segmentLength = i - segmentBegin;
        if(segmentLength == 0u)
            return false;

        const AStringView segment(pathText.data() + segmentBegin, segmentLength);
        if(segment == "." || segment == "..")
            return false;

        segmentBegin = i + 1u;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

